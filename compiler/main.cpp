#include <array>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include "asm.hpp"
#include "ast.hpp"
#include "source.hpp"
#include "magic_enum.hpp"
#include "token.hpp"

using namespace std;

namespace {

size_t label_counter{0};
string GenerateLabel() {
  ostringstream oss;
  oss << "LABEL" << label_counter++;
  return oss.str();
}

const array<Asm::Register, 6> kArgRegs{
  Asm::kRegArg0, Asm::kRegArg1, Asm::kRegArg2,
  Asm::kRegArg3, Asm::kRegArg4, Asm::kRegArg5
};

Context* cur_ctx; // 現在コード生成中の関数を表すコンテキスト

map<Symbol*, Node*> gvar_init_values;
vector<Node*> string_literal_nodes;

Asm* asmgen;

} // namespace

// シンボルのアドレスを rax に書く
void LoadSymAddr(ostream& os, Token* sym_id) {
  auto sym{LookupSymbol(cur_ctx, sym_id->Raw())};

  if (sym == nullptr) {
    cerr << "undeclared symbol '" << sym_id->Raw() << "'" << endl;
    ErrorAt(sym_id->loc);
  }

  if (sym->kind == Symbol::kLVar) {
    asmgen->LEA(os, Asm::kRegL, Asm::kRegBP, -sym->offset);
    return;
  }

  if (sym->kind == Symbol::kFunc) {
    asmgen->LoadSymAddr(os, Asm::kRegL, sym->token->Raw());
    return;
  }

  // グローバルなシンボル
  asmgen->LoadSymAddr(os, Asm::kRegL, sym->token->Raw());
}

void GenerateAsm(ostream& os, Node* node,
                 string_view label_break, string_view label_cont,
                 bool lval = false) {
  switch (node->kind) {
  case Node::kInt:
    asmgen->Push64(os, node->value.i);
    return;
  case Node::kId:
    LoadSymAddr(os, node->token);
    if (lval) {
      asmgen->Push64(os, Asm::kRegL);
    } else if (auto [is_int, t] = IsInteger(node->value.sym->type); is_int) {
      asmgen->LoadPushN(os, Asm::kRegL, t->num);
    } else {
      asmgen->LoadPushN(os, Asm::kRegL, 64);
    }
    return;
  case Node::kRet:
    GenerateAsm(os, node->lhs, label_break, label_cont);
    asmgen->Pop64(os, Asm::kRegRet);
    asmgen->Jmp(os, cur_ctx->func_name + "_exit");
    return;
  case Node::kIf:
    {
      auto label_else{GenerateLabel()};
      auto label_exit{GenerateLabel()};
      asmgen->Push64(os, Asm::kRegL);
      GenerateAsm(os, node->cond, label_break, label_cont);
      asmgen->Pop64(os, Asm::kRegL);
      asmgen->JmpIfZero(os, Asm::kRegL, label_else);
      asmgen->Pop64(os, Asm::kRegL);
      GenerateAsm(os, node->lhs, label_break, label_cont);
      asmgen->Jmp(os, label_exit);
      os << label_else << ":\n";
      if (node->rhs) {
        GenerateAsm(os, node->rhs, label_break, label_cont);
      }
      os << label_exit << ":\n";
    }
    return;
  case Node::kLoop:
    {
      auto label_loop{GenerateLabel()};
      auto label_next{GenerateLabel()};
      auto label_end{GenerateLabel()};
      os << label_loop << ":\n";
      GenerateAsm(os, node->lhs, label_end, label_next);
      os << label_next << ":\n";
      asmgen->Pop64(os, Asm::kRegL);
      asmgen->Jmp(os, label_loop);
      os << label_end << ":\n";
    }
    return;
  case Node::kFor:
    // for init; cond; succ { ... }
    // init: node->rhs
    // cond: node->cond
    // succ: node->rhs->next
    {
      auto label_loop{GenerateLabel()};
      auto label_cond{GenerateLabel()};
      auto label_next{GenerateLabel()};
      auto label_end{GenerateLabel()};
      if (node->rhs) {
        GenerateAsm(os, node->rhs, label_end, label_next);
        asmgen->Pop64(os, Asm::kRegL);
      }
      asmgen->LoadPushN(os, Asm::kRegSP, 64);
      asmgen->Jmp(os, label_cond);
      os << label_loop << ":\n";
      asmgen->Pop64(os, Asm::kRegL);
      GenerateAsm(os, node->lhs, label_end, label_next);
      os << label_next << ":\n";
      if (node->rhs) {
        GenerateAsm(os, node->rhs->next, label_end, label_next);
        asmgen->Pop64(os, Asm::kRegL);
      }
      os << label_cond << ":\n";
      GenerateAsm(os, node->cond, label_end, label_next);
      asmgen->Pop64(os, Asm::kRegL);
      asmgen->JmpIfNotZero(os, Asm::kRegL, label_loop);
      os << label_end << ":\n";
    }
    return;
  case Node::kBlock:
    if (node->next == nullptr) {
      asmgen->Push64(os, Asm::kRegL);
      return;
    }
    for (node = node->next; node != nullptr; node = node->next) {
      GenerateAsm(os, node, label_break, label_cont);
      if (node->next) {
        asmgen->Pop64(os, Asm::kRegL);
      }
    }
    return;
  case Node::kCall:
    {
      if (auto t = FindType(node->lhs->token);
          node->lhs->kind == Node::kId && t) {
        // 型変換
        GenerateAsm(os, node->rhs->next, label_break, label_cont);
        if (auto [is_int, int_type] = IsInteger(t);
            is_int && int_type->num < 64) {
          asmgen->Pop64(os, Asm::kRegL);
          asmgen->MaskBits(os, Asm::kRegL, int_type->num);
          asmgen->Push64(os, Asm::kRegL);
        }
        return;
      }

      if (node->lhs->type == nullptr) {
        cerr << "node->lhs->type cannot be null" << endl;
        ErrorAt(node->lhs->token->loc);
      }

      Type* func_type = nullptr;
      switch (node->lhs->type->kind) {
      case Type::kFunc:
        func_type = node->lhs->type;
        break;
      case Type::kPointer:
        func_type = node->lhs->type->base;
        if (func_type->kind != Type::kFunc) {
          cerr << "cannot call non-function pointer" << endl;
          ErrorAt(node->lhs->token->loc);
        }
        break;
      default:
        cerr << "node->lhs = "
             << magic_enum::enum_name(node->lhs->kind)
             << ", " << node->lhs->token->Raw() << endl;
        cerr << "cannot call "
             << magic_enum::enum_name(node->lhs->type->kind) << endl;
        ErrorAt(node->lhs->token->loc);
      }

      vector<Node*> args;
      vector<Type*> param_types;

      auto param_type{func_type->next};
      for (auto arg = node->rhs->next; arg; arg = arg->next) {
        args.push_back(arg);
        if (param_type) {
          param_types.push_back(param_type);
          param_type = param_type->next;
        }
      }
      const bool has_vparam =
        !param_types.empty() &&
        param_types[param_types.size() - 1]->kind == Type::kVParam;

      if (param_types.empty() && !args.empty()) {
        cerr << "too many arguments" << endl;
        ErrorAt(args[0]->token->loc);
      } else if (args.size() < param_types.size() - has_vparam) {
        cerr << "too few arguments" << endl;
        ErrorAt(args[args.size() - 1]->token->loc);
      } else if (args.size() > kArgRegs.size()) {
        cerr << "# of arguments must be <= " << kArgRegs.size() << endl;
        ErrorAt(args[kArgRegs.size()]->token->loc);
      }

      for (auto it = args.rbegin(); it != args.rend(); ++it) {
        GenerateAsm(os, *it, label_break, label_cont);
      }
      if (has_vparam) {
        size_t num_normal = param_types.size() - 1;
        asmgen->PrepareFuncVArg(os, num_normal, args.size() - num_normal);
      }

      if (node->lhs->kind == Node::kId) {
        LoadSymAddr(os, node->lhs->token);
        auto func_sym{LookupSymbol(cur_ctx, node->lhs->token->Raw())};
        if (func_sym->type->kind == Type::kPointer) {
          asmgen->Load64(os, Asm::kRegL, Asm::kRegL);
        } else if (func_sym->type->kind != Type::kFunc) {
          cerr << "cannot call "
               << magic_enum::enum_name(func_sym->type->kind) << endl;
          ErrorAt(node->lhs->token->loc);
        }
      } else {
        GenerateAsm(os, node->lhs, label_break, label_cont);
        asmgen->Pop64(os, Asm::kRegL);
      }

      size_t arg_on_reg_end = args.size();
      if (has_vparam && asmgen->FuncVArgOnStack()) {
        arg_on_reg_end = param_types.size() - 1;
      }
      for (size_t i = 0; i < arg_on_reg_end; ++i) {
        asmgen->Pop64(os, kArgRegs[i]);
      }

      asmgen->Call(os, Asm::kRegL);
      for (size_t i = arg_on_reg_end; i < args.size(); ++i) {
        asmgen->Pop64(os, Asm::kRegR);
      }
      asmgen->Push64(os, Asm::kRegRet);
    }
    return;
  case Node::kDeclSeq:
    for (auto decl{node->next}; decl != nullptr; decl = decl->next) {
      cur_ctx = contexts[decl->token->Raw()];
      GenerateAsm(os, decl, label_break, label_cont);
    }
    return;
  case Node::kDefFunc:
    asmgen->FuncPrologue(os, cur_ctx);
    for (size_t i = 0; i < cur_ctx->params.size(); ++i) {
      auto off{cur_ctx->params[i]->offset};
      asmgen->StoreN(os, Asm::kRegBP, -off, kArgRegs[i], 64);
    }
    GenerateAsm(os, node->lhs, label_break, label_cont); // 関数ボディ
    asmgen->FuncEpilogue(os, cur_ctx);
    return;
  case Node::kDefVar:
    if (node->lhs->value.sym->kind == Symbol::kGVar) {
      gvar_init_values[node->lhs->value.sym] = node->rhs;
      return;
    } else if (node->rhs && node->rhs->kind == Node::kInitList) {
      auto stride{Sizeof(node->lhs->token, node->lhs->type->base)};
      auto elem{node->rhs->next}; // 初期化リストの先頭要素
      int64_t i;
      for (i = 0; i < node->rhs->value.i; ++i) {
        GenerateAsm(os, elem, label_break, label_cont);
        GenerateAsm(os, node->lhs, label_break, label_cont, true);
        asmgen->Pop64(os, Asm::kRegL);
        asmgen->Pop64(os, Asm::kRegR);
        asmgen->StoreN(os, Asm::kRegL, stride * i, Asm::kRegR, 8 * stride);
        elem = elem->next;
      }
      for (; i < node->lhs->type->num; ++i) {
        GenerateAsm(os, node->lhs, label_break, label_cont, true);
        asmgen->Pop64(os, Asm::kRegL);
        asmgen->StoreN(os, Asm::kRegL, stride * i, Asm::kRegZero, 8 * stride);
      }
    } else if (node->rhs) { // 初期値付き変数定義
      break;
    }
    asmgen->Push64(os, Asm::kRegL); // 初期値無し変数定義用 dummy push
    return;
  case Node::kExtern:
    return;
  case Node::kStr:
    {
      ostringstream oss;
      oss << "STR" << string_literal_nodes.size();
      string_literal_nodes.push_back(node);
      asmgen->LoadSymAddr(os, Asm::kRegL, oss.str());
      asmgen->Push64(os, Asm::kRegL);
    }
    return;
  case Node::kSizeof:
    asmgen->Push64(os, Sizeof(node->lhs->token, node->lhs->type));
    return;
  case Node::kLOr:
    {
      auto label_true{GenerateLabel()};
      auto label_false{GenerateLabel()};
      GenerateAsm(os, node->lhs, label_break, label_cont);
      asmgen->Pop64(os, Asm::kRegL);
      asmgen->JmpIfNotZero(os, Asm::kRegL, label_true);
      GenerateAsm(os, node->rhs, label_break, label_cont);
      asmgen->Pop64(os, Asm::kRegL);
      asmgen->JmpIfZero(os, Asm::kRegL, label_false);
      os << label_true << ":\n";
      asmgen->Mov64(os, Asm::kRegL, 1);
      os << label_false << ":\n";
      asmgen->Push64(os, Asm::kRegL);
    }
    return;
  case Node::kLAnd:
    {
      auto label_false{GenerateLabel()};
      GenerateAsm(os, node->lhs, label_break, label_cont);
      asmgen->Pop64(os, Asm::kRegL);
      asmgen->JmpIfZero(os, Asm::kRegL, label_false);
      GenerateAsm(os, node->rhs, label_break, label_cont);
      asmgen->Pop64(os, Asm::kRegL);
      asmgen->JmpIfZero(os, Asm::kRegL, label_false);
      asmgen->Mov64(os, Asm::kRegL, 1);
      os << label_false << ":\n";
      asmgen->Push64(os, Asm::kRegL);
    }
    return;
  case Node::kBreak:
    asmgen->Push64(os, Asm::kRegL);
    asmgen->Jmp(os, label_break);
    return;
  case Node::kCont:
    asmgen->Push64(os, Asm::kRegL);
    asmgen->Jmp(os, label_cont);
    return;
  case Node::kTypedef:
    return;
  case Node::kInc: // fall-through
  case Node::kDec:
    GenerateAsm(os, node->lhs, label_break, label_cont, true);
    asmgen->Pop64(os, Asm::kRegL);
    if (node->kind == Node::kInc) {
      asmgen->Inc64(os, Asm::kRegL);
    } else {
      asmgen->Dec64(os, Asm::kRegL);
    }
    asmgen->Push64(os, Asm::kRegL);
    return;
  default: // caseが足りないという警告を抑制する
    break;
  }

  const bool request_lval{
    node->kind == Node::kAssign ||
    node->kind == Node::kAddr ||
    node->kind == Node::kDefVar ||
    (node->kind == Node::kSubscr && node->lhs->type->kind == Type::kArray)
  };

  GenerateAsm(os, node->lhs, label_break, label_cont, request_lval);
  if (node->rhs) {
    // 単項演算子の場合は rhs が null の場合がある
    GenerateAsm(os, node->rhs, label_break, label_cont);
    asmgen->Pop64(os, Asm::kRegR);
  }
  asmgen->Pop64(os, Asm::kRegL);

  switch (node->kind) {
  case Node::kAdd:
    if (auto [is_int, t] = IsInteger(node->type); is_int) { // int + int
      asmgen->Add64(os, Asm::kRegL, Asm::kRegR);
      asmgen->MaskBits(os, Asm::kRegL, t->num);
    } else if (node->type->kind == Type::kPointer) { // int + ptr, ptr + int
      const auto scale{Sizeof(node->token, node->type->base)};
      asmgen->LEA(os, Asm::kRegL, Asm::kRegL, scale, Asm::kRegR);
    }
    break;
  case Node::kSub:
    if (auto [is_int, t] = IsInteger(node->type); is_int) {
      if (node->lhs->type->kind == Type::kPointer) { // ptr - ptr
        asmgen->Sub64(os, Asm::kRegL, Asm::kRegR);
        asmgen->ShiftR(os, Asm::kRegL, 3);
      } else {                                       // int - int
        asmgen->Sub64(os, Asm::kRegL, Asm::kRegR);
        asmgen->MaskBits(os, Asm::kRegL, t->num);
      }
    } else if (node->type->kind == Type::kPointer) { // ptr - int
      const auto scale{Sizeof(node->token, node->type->base)};
      asmgen->LEA(os, Asm::kRegL, Asm::kRegL, -scale, Asm::kRegR);
    }
    break;
  case Node::kMul:
    asmgen->IMul64(os, Asm::kRegL, Asm::kRegR);
    break;
  case Node::kDiv:
    asmgen->IDiv64(os, Asm::kRegL, Asm::kRegR);
    break;
  case Node::kEqu:
    asmgen->CmpSet(os, Asm::kCmpE, Asm::kRegL, Asm::kRegL, Asm::kRegR);
    break;
  case Node::kNEqu:
    asmgen->CmpSet(os, Asm::kCmpNE, Asm::kRegL, Asm::kRegL, Asm::kRegR);
    break;
  case Node::kGT:
    if (node->lhs->type->kind == Type::kUInt) {
      asmgen->CmpSet(os, Asm::kCmpA, Asm::kRegL, Asm::kRegL, Asm::kRegR);
    } else {
      asmgen->CmpSet(os, Asm::kCmpG, Asm::kRegL, Asm::kRegL, Asm::kRegR);
    }
    break;
  case Node::kLE:
    if (node->lhs->type->kind == Type::kUInt) {
      asmgen->CmpSet(os, Asm::kCmpBE, Asm::kRegL, Asm::kRegL, Asm::kRegR);
    } else {
      asmgen->CmpSet(os, Asm::kCmpLE, Asm::kRegL, Asm::kRegL, Asm::kRegR);
    }
    break;
  case Node::kAssign:
  case Node::kDefVar:
    if (auto [is_int, t] = IsInteger(node->lhs->type); is_int) {
      auto bits = t->num;
      asmgen->MaskBits(os, Asm::kRegR, bits);
      asmgen->StoreN(os, Asm::kRegL, 0, Asm::kRegR, bits);
    } else {
      asmgen->StoreN(os, Asm::kRegL, 0, Asm::kRegR, 64);
    }
    asmgen->Push64(os, lval ? Asm::kRegL : Asm::kRegR);
    return;
  case Node::kAddr:
    // pass
    break;
  case Node::kDeref:
    if (!lval) {
      asmgen->Load64(os, Asm::kRegL, Asm::kRegL);
    }
    break;
  case Node::kSubscr:
    {
      auto scale{Sizeof(node->token, node->type)};
      if (lval) {
        asmgen->LEA(os, Asm::kRegL, Asm::kRegL, scale, Asm::kRegR);
      } else {
        if (scale == 1 || scale == 2 || scale == 4 || scale == 8) {
          asmgen->LoadN(os, Asm::kRegL, Asm::kRegL, scale, Asm::kRegR);
        } else {
          cerr << "non-standard scale is not supported: " << scale << endl;
          ErrorAt(node->token->loc);
        }
      }
    }
    break;
  default: // caseが足りないという警告を抑制する
    break;
  }

  asmgen->Push64(os, Asm::kRegL);
}

int ParseArgs(int argc, char** argv) {
  int i = 1;
  while (i < argc) {
    if (strcmp(argv[i], "-target-arch") == 0) {
      if (i == argc - 1) {
        cerr << "-target-arch needs one argument" << endl;
        return 1;
      }
      std::string_view arch = argv[i + 1];
      if (arch == "x86_64") {
        asmgen = new AsmX8664;
      } else if (arch == "aarch64") {
        asmgen = new AsmAArch64;
      } else {
        cerr << "unknown target architecture: " << arch << endl;
        return 1;
      }
      i += 2;
    } else {
      cerr << "unknown argument: " << argv[i] << endl;
      return 1;
    }
  }
  return 0;
}

int main(int argc, char** argv) {
  if (int err = ParseArgs(argc, argv)) {
    return err;
  }
  if (asmgen == nullptr) {
    asmgen = new AsmX8664;
  }

  ReadAll(cin);
  auto tokens{Tokenize(&src[0])};
  cur_token = tokens.begin();
  auto ast{Program()};

  for (auto [ type_name, type ] : types) {
    auto it{undeclared_id_nodes.begin()};
    while (it != undeclared_id_nodes.end()) {
      auto node{*it};
      if (type_name == node->token->Raw()) {
        node->type = type;
        it = undeclared_id_nodes.erase(it);
      } else {
        ++it;
      }
    }
  }

  if (!undeclared_id_nodes.empty()) {
    cerr << "undeclared ids are used:";
    for (auto node : undeclared_id_nodes) {
      cerr << ' ' << node->token->Raw();
    }
    cerr << endl;
    return 1;
  }
  while (SetSymbolType(ast));

  for (auto [ name, ctx ] : contexts) {
    CalcStackOffset(ctx->local_vars,
                    [](Symbol* lvar, size_t offset) {
                      lvar->offset = offset;
                    });
  }

  asmgen->SectionText(cout);
  GenerateAsm(cout, ast, "", "");

  for (auto [ name, sym ] : symbols) {
    if (sym->kind == Symbol::kEVar || sym->kind == Symbol::kEFunc) {
      cout << ".extern " << asmgen->SymLabel(name) << "\n";
    }
  }

  const std::array<const char*, 9> size_map{
    nullptr,
    ".byte",
    ".2byte",
    nullptr,
    ".4byte",
    nullptr,
    nullptr,
    nullptr,
    ".8byte",
  };

  if (!gvar_init_values.empty()) {
    asmgen->FuncPrologue(cout, "_init_opela");
    for (auto [ sym, init ] : gvar_init_values) {
      if (init && init->kind != Node::kInt) {
        GenerateAsm(cout, init, "", "");
        asmgen->Pop64(cout, Asm::kRegL);
        asmgen->StoreN(cout, sym->token->Raw(), Asm::kRegL, 64);
      }
    }
    asmgen->FuncEpilogue(cout);
    asmgen->SectionInit(cout);
    cout << "    .dc.a " << asmgen->SymLabel("_init_opela") << "\n";

    asmgen->SectionData(cout, false);
    for (auto [ sym, init ] : gvar_init_values) {
      cout << asmgen->SymLabel(sym->token->Raw()) << ":\n";
      cout << "    " << size_map[Sizeof(sym->token, sym->type)] << ' ';
      if (init && init->kind == Node::kInt) {
        cout << init->value.i << "\n";
      } else {
        cout << "0\n";
      }
    }
  }

  if (!string_literal_nodes.empty()) {
    asmgen->SectionData(cout, true);
    for (size_t i{0}; i < string_literal_nodes.size(); ++i) {
      auto node{string_literal_nodes[i]};
      cout << asmgen->SymLabel("STR") << i << ":\n    .byte ";
      for (size_t j{0}; j < node->value.str.len; ++j) {
        cout << static_cast<int>(node->value.str.data[j]) << ',';
      }
      cout << "0\n";
    }
  }
}
