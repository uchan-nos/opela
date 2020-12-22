#include <array>
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

const array<const char*, 6> kArgRegs{"rdi", "rsi", "rdx", "rcx", "r8", "r9"};

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
    } else if (node->value.sym->type->kind != Type::kInt) {
      os << "    push qword ptr [rax]\n";
    } else {
      switch (node->value.sym->type->num) {
      case 8:
        os << "    xor edi, edi\n";
        os << "    mov dil, [rax]\n";
        os << "    push rdi\n";
        break;
      case 16:
        os << "    xor edi, edi\n";
        os << "    mov di [rax]\n";
        os << "    push rdi\n";
        break;
      case 32:
        os << "    mov edi, [rax]\n";
        os << "    push rdi\n";
        break;
      case 64:
        asmgen->LoadPush64(os, Asm::kRegL);
        break;
      default:
        cerr << "loading non-standard size integer is not supported" << endl;
        ErrorAt(node->token->loc);
      }
    }
    return;
  case Node::kRet:
    GenerateAsm(os, node->lhs, label_break, label_cont);
    asmgen->Pop64(os, Asm::kRegL);
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
      os << "    pop rax\n";
      os << "    jmp " << label_loop << "\n";
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
      asmgen->LoadPush64(os, Asm::kRegSP);
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
      if (node->lhs->kind == Node::kId && types[node->lhs->token->Raw()]) {
        // 型変換
        GenerateAsm(os, node->rhs->next, label_break, label_cont);
        return;
      }

      if (node->lhs->type == nullptr) {
        cerr << "node->lhs->type cannot be null" << endl;
        ErrorAt(node->lhs->token->loc);
      }
      switch (node->lhs->type->kind) {
      case Type::kFunc:
        break;
      case Type::kPointer:
        if (node->lhs->type->base->kind != Type::kFunc) {
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

      int num_arg{0};
      for (auto arg{node->rhs->next}; arg != nullptr; arg = arg->next) {
        if (num_arg == kArgRegs.size()) {
          cerr << "# of arguments must be <= " << kArgRegs.size() << endl;
          ErrorAt(arg->token->loc);
        }
        GenerateAsm(os, arg, label_break, label_cont);
        ++num_arg;
      }

      if (node->lhs->kind == Node::kId) {
        LoadSymAddr(os, node->lhs->token);
        auto func_sym{LookupSymbol(cur_ctx, node->lhs->token->Raw())};
        if (func_sym->type->kind == Type::kPointer) {
          os << "    mov rax, [rax]\n";
        } else if (func_sym->type->kind != Type::kFunc) {
          cerr << "cannot call "
               << magic_enum::enum_name(func_sym->type->kind) << endl;
          ErrorAt(node->lhs->token->loc);
        }
      } else {
        GenerateAsm(os, node->lhs, label_break, label_cont);
        asmgen->Pop64(os, Asm::kRegL);
      }

      for (; num_arg > 0; --num_arg) {
        os << "    pop " << kArgRegs[num_arg - 1] << "\n";
      }

      asmgen->Call(os, Asm::kRegL);
      asmgen->Push64(os, Asm::kRegL);
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
      os << "    mov [rbp - " << off << "], " << kArgRegs[i] << "\n";
    }
    GenerateAsm(os, node->lhs, label_break, label_cont); // 関数ボディ
    asmgen->FuncEpilogue(os, cur_ctx);
    return;
  case Node::kDefVar:
    if (node->lhs->value.sym->kind == Symbol::kGVar) {
      gvar_init_values[node->lhs->value.sym] = node->rhs;
      return;
    }
    if (node->rhs) { // 初期値付き変数定義
      break;
    }
    os << "    push rax\n"; // dummy push
    return;
  case Node::kExtern:
    return;
  case Node::kStr:
    {
      ostringstream oss;
      oss << "STR" << string_literal_nodes.size();
      string_literal_nodes.push_back(node);
      os << "    movabs rax, offset " << oss.str() << "\n";
      os << "    push rax\n";
    }
    return;
  case Node::kSizeof:
    os << "    push "
       << Sizeof(node->lhs->token, node->lhs->type) << "\n";
    return;
  case Node::kLOr:
    {
      auto label_true{GenerateLabel()};
      auto label_false{GenerateLabel()};
      GenerateAsm(os, node->lhs, label_break, label_cont);
      os << "    pop rax\n";
      os << "    test rax, rax\n";
      os << "    jnz " << label_true << "\n";
      GenerateAsm(os, node->rhs, label_break, label_cont);
      os << "    pop rax\n";
      os << "    test rax, rax\n";
      os << "    jz " << label_false << "\n";
      os << label_true << ":\n";
      os << "    mov rax, 1\n";
      os << label_false << ":\n";
      os << "    push rax\n";
    }
    return;
  case Node::kLAnd:
    {
      auto label_false{GenerateLabel()};
      GenerateAsm(os, node->lhs, label_break, label_cont);
      os << "    pop rax\n";
      os << "    test rax, rax\n";
      os << "    jz " << label_false << "\n";
      GenerateAsm(os, node->rhs, label_break, label_cont);
      os << "    pop rax\n";
      os << "    test rax, rax\n";
      os << "    jz " << label_false << "\n";
      os << "    mov rax, 1\n";
      os << label_false << ":\n";
      os << "    push rax\n";
    }
    return;
  case Node::kBreak:
    os << "    push rax\n";
    os << "    jmp " << label_break << "\n";
    return;
  case Node::kCont:
    os << "    push rax\n";
    os << "    jmp " << label_cont << "\n";
    return;
  case Node::kTypedef:
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
    if (node->type->kind == Type::kInt) {
      asmgen->Add64(os, Asm::kRegL, Asm::kRegR);
    } else if (node->type->kind == Type::kPointer) {
      const auto scale{Sizeof(node->token, node->type->base)};
      os << "    lea rax, [rax + " << scale << " * rdi]\n";
    } else if (node->type->kind == Type::kUser &&
               node->type->base->kind == Type::kInt) {
      os << "    add rax, rdi\n";
    }
    break;
  case Node::kSub:
    if (node->type->kind == Type::kInt) {
      if (node->lhs->type->kind == Type::kPointer) { // ptr - ptr
        os << "    sub rax, rdi\n";
        os << "    shr rax, 3\n";
      } else {                                       // int - int
        asmgen->Sub64(os, Asm::kRegL, Asm::kRegR);
      }
    } else if (node->type->kind == Type::kPointer) { // ptr - int
      const auto scale{Sizeof(node->token, node->type->base)};
      os << "    neg rdi\n";
      os << "    lea rax, [rax + " << scale << " * rdi]\n";
    } else if (node->type->kind == Type::kUser &&
               node->type->base->kind == Type::kInt) {
      os << "    sub rax, rdi\n";
    }
    break;
  case Node::kMul:
    asmgen->IMul64(os, Asm::kRegL, Asm::kRegR);
    break;
  case Node::kDiv:
    asmgen->IDiv64(os, Asm::kRegL, Asm::kRegR);
    break;
  case Node::kEqu:
    asmgen->CmpSet(os, Asm::kCmpE, Asm::kRegL, Asm::kRegR);
    break;
  case Node::kNEqu:
    asmgen->CmpSet(os, Asm::kCmpNE, Asm::kRegL, Asm::kRegR);
    break;
  case Node::kLT:
    asmgen->CmpSet(os, Asm::kCmpL, Asm::kRegL, Asm::kRegR);
    break;
  case Node::kLE:
    asmgen->CmpSet(os, Asm::kCmpLE, Asm::kRegL, Asm::kRegR);
    break;
  case Node::kAssign:
  case Node::kDefVar:
    if (node->lhs->type->kind != Type::kInt) {
      os << "    mov [rax], rdi\n";
    } else {
      switch (node->lhs->type->num) {
      case 8:
        os << "    mov [rax], dil\n";
        break;
      case 16:
        os << "    mov [rax], di\n";
        break;
      case 32:
        os << "    mov [rax], edi\n";
        break;
      case 64:
        asmgen->Store64(os, Asm::kRegL, Asm::kRegR);
        break;
      default:
        cerr << "non-standard size assignment is not supported" << endl;
        ErrorAt(node->token->loc);
      }
    }
    asmgen->Push64(os, lval ? Asm::kRegL : Asm::kRegR);
    return;
  case Node::kAddr:
    // pass
    break;
  case Node::kDeref:
    if (!lval) {
      os << "    mov rax, [rax]\n";
    }
    break;
  case Node::kSubscr:
    {
      auto scale{Sizeof(node->token, node->type)};
      if (lval) {
        os << "    lea rax, [rax + " << scale << " * rdi]\n";
      } else {
        if (scale == 1) {
          os << "    movzx eax, byte ptr [rax + " << scale << " * rdi]\n";
        } else if (scale == 2) {
          os << "    movzx eax, word ptr [rax + " << scale << " * rdi]\n";
        } else if (scale == 4) {
          os << "    mov eax, [rax + " << scale << " * rdi]\n";
        } else if (scale == 8) {
          os << "    mov rax, [rax + " << scale << " * rdi]\n";
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
    size_t offset{0};
    for (auto [ name, lvar ] : ctx->local_vars) {
      offset += Sizeof(lvar->token, lvar->type);
      lvar->offset = offset;
    }
  }

  ostringstream oss;
  GenerateAsm(oss, ast, "", "");

  asmgen->SectionText(cout);
  cout << oss.str();

  for (auto [ name, sym ] : symbols) {
    if (sym->kind == Symbol::kEVar || sym->kind == Symbol::kEFunc) {
      asmgen->ExternSym(cout, name);
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
    cout << "_init_opela:\n";
    cout << "    push rbp\n";
    cout << "    mov rbp, rsp\n";
    for (auto [ sym, init ] : gvar_init_values) {
      if (init && init->kind != Node::kInt) {
        GenerateAsm(cout, init, "", "");
        cout << "    pop rax\n";
        cout << "    mov [" << sym->token->Raw() << "], rax\n";
      }
    }
    cout << "    mov rsp, rbp\n";
    cout << "    pop rbp\n";
    cout << "    ret\n";
    cout << ".section .init_array\n";
    cout << "    .dc.a _init_opela\n";

    cout << ".section .data\n";
    for (auto [ sym, init ] : gvar_init_values) {
      cout << sym->token->Raw() << ":\n";
      cout << "    " << size_map[Sizeof(sym->token, sym->type)] << ' ';
      if (init && init->kind == Node::kInt) {
        cout << init->value.i << "\n";
      } else {
        cout << "0\n";
      }
    }
  }

  for (size_t i{0}; i < string_literal_nodes.size(); ++i) {
    auto node{string_literal_nodes[i]};
    cout << "STR" << i << ":\n    .byte ";
    for (size_t j{0}; j < node->value.str.len; ++j) {
      cout << static_cast<int>(node->value.str.data[j]) << ',';
    }
    cout << "0\n";
  }
}
