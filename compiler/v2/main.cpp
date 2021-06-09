#include <algorithm>
#include <array>
#include <bit>
#include <bitset>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include "asm.hpp"
#include "ast.hpp"
#include "magic_enum.hpp"
#include "object.hpp"
#include "source.hpp"
#include "token.hpp"

using namespace std;

namespace {

int verbosity = 0;
string target_arch = "x86_64";

int ParseArgs(int argc, char** argv) {
  int i = 1;
  while (i < argc) {
    string_view opt = argv[i];
    if (opt == "-target-arch") {
      if (i == argc - 1) {
        cerr << "-target-arch needs one argument" << endl;
        return 1;
      }
      target_arch = argv[i + 1];
      i += 2;
    } else if (opt == "-v") {
      ++verbosity;
      ++i;
    } else {
      cerr << "unknown argument: " << opt << endl;
      return 1;
    }
  }
  return 0;
}

} // namespace

Asm::Register UseAnyCalcReg(Asm::RegSet& free_calc_regs) {
  auto reg_index = countr_zero(free_calc_regs.to_ulong());
  if (reg_index >= Asm::kRegNum) {
    return Asm::kRegNum;
  }
  free_calc_regs.reset(reg_index);
  return static_cast<Asm::Register>(reg_index);
}

bool HasChildren(Node* node) {
  if (node == nullptr) {
    return false;
  } else if (node->kind == Node::kInt) {
    return false;
  }
  return true;
}

int SetErshovNumber(Source& src, Node* expr) {
  if (expr->ershov > 0) {
    return expr->ershov;
  } else if (expr->kind == Node::kCall) {
    for (auto arg = expr->rhs; arg; arg = arg->next) {
      SetErshovNumber(src, arg);
    }
    return expr->ershov = 9;
  } else if (expr->lhs == nullptr && expr->rhs == nullptr) {
    return expr->ershov = 1;
  } else if (expr->lhs != nullptr && expr->rhs != nullptr) {
    int l = SetErshovNumber(src, expr->lhs);
    int r = SetErshovNumber(src, expr->rhs);
    return expr->ershov = l == r ? l + 1 : max(l, r);
  } else if (expr->lhs != nullptr && expr->rhs == nullptr) {
    return expr->ershov = SetErshovNumber(src, expr->lhs);
  }
  cerr << "unexpected node" << endl;
  ErrorAt(src, *expr->token);
}

struct GenContext {
  Source& src;
  Asm& asmgen;
  Object* func;
};

size_t label_counter = 0;
string GenerateLabel() {
  ostringstream oss;
  oss << "LABEL" << label_counter++;
  return oss.str();
}

string StringLabel(size_t index) {
  ostringstream oss;
  oss << "STR" << index;
  return oss.str();
}

const std::array<const char*, 9> kSizeMap{
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

void GenerateAsm(GenContext& ctx, Node* node,
                 Asm::Register dest, Asm::RegSet free_calc_regs,
                 bool lval = false) {
  auto comment_node = [ctx, node]{
    ctx.asmgen.Output() << "    # ";
    PrintAST(ctx.asmgen.Output(), node);
    ctx.asmgen.Output() << '\n';
  };

  switch (node->kind) {
  case Node::kInt:
    comment_node();
    ctx.asmgen.Mov64(dest, get<opela_type::Int>(node->value));
    return;
  case Node::kBlock:
    for (auto stmt = node->next; stmt; stmt = stmt->next) {
      GenerateAsm(ctx, stmt, dest, free_calc_regs);
    }
    return;
  case Node::kId:
    comment_node();
    if (auto p = get_if<Object*>(&node->value)) {
      Object* obj = *p;
      switch (obj->linkage) {
      case Object::kLocal:
        if (lval) {
          ctx.asmgen.LEA(dest, Asm::kRegBP, obj->bp_offset);
        } else {
          ctx.asmgen.Load64(dest, Asm::kRegBP, obj->bp_offset);
        }
        break;
      case Object::kGlobal:
      case Object::kExternal:
        if (lval || obj->kind == Object::kFunc) {
          ctx.asmgen.LoadLabelAddr(dest, obj->id->raw);
        } else {
          ctx.asmgen.Load64(dest, obj->id->raw);
        }
        break;
      }
    }
    return;
  case Node::kDefVar:
    comment_node();
    GenerateAsm(ctx, node->rhs, dest, free_calc_regs);
    ctx.asmgen.Store64(
        Asm::kRegBP, get<Object*>(node->lhs->value)->bp_offset, dest);
    return;
  case Node::kDefFunc:
    {
      auto func = get<Object*>(node->value);
      GenContext func_ctx{ctx.src, ctx.asmgen, func};

      int stack_size = 0;
      for (Object* obj : func->locals) {
        stack_size += 8;
        obj->bp_offset = -stack_size;
      }
      stack_size = (stack_size + 0xf) & ~static_cast<size_t>(0xf);

      ctx.asmgen.Output() << ".global " << func->id->raw << '\n'
                          << func->id->raw << ":\n";
      ctx.asmgen.Push64(Asm::kRegBP);
      ctx.asmgen.Mov64(Asm::kRegBP, Asm::kRegSP);
      ctx.asmgen.Sub64(Asm::kRegSP, stack_size);
      int arg_index = 0;
      for (auto param = node->rhs; param; param = param->next) {
        auto arg_reg = static_cast<Asm::Register>(Asm::kRegV0 + arg_index);
        ctx.asmgen.Store64(Asm::kRegBP, -8 * (1 + arg_index), arg_reg);
        ++arg_index;
      }
      GenerateAsm(func_ctx, node->lhs, dest, free_calc_regs);
      ctx.asmgen.Xor64(Asm::kRegA, Asm::kRegA);
      ctx.asmgen.Output() << func->id->raw << ".exit:\n";
      ctx.asmgen.Leave();
      ctx.asmgen.Ret();
      return;
    }
  case Node::kRet:
    comment_node();
    if (node->lhs) {
      GenerateAsm(ctx, node->lhs, dest, free_calc_regs);
    }
    ctx.asmgen.Jmp(string{ctx.func->id->raw} + ".exit");
    return;
  case Node::kIf:
    comment_node();
    {
      auto label_exit = GenerateLabel();
      auto label_else = node->rhs ? GenerateLabel() : label_exit;
      GenerateAsm(ctx, node->cond, dest, free_calc_regs);
      ctx.asmgen.JmpIfZero(dest, label_else);
      GenerateAsm(ctx, node->lhs, dest, free_calc_regs);
      if (node->rhs) {
        ctx.asmgen.Jmp(label_exit);
        ctx.asmgen.Output() << label_else << ": # else clause\n";
        GenerateAsm(ctx, node->rhs, dest, free_calc_regs);
      }
      ctx.asmgen.Output() << label_exit << ": # if stmt exit\n";
    }
    return;
  case Node::kFor:
    comment_node();
    {
      auto label_loop = GenerateLabel();
      auto label_cond = GenerateLabel();
      auto label_next = node->rhs ? GenerateLabel() : label_cond;
      if (node->rhs) {
        GenerateAsm(ctx, node->rhs, dest, free_calc_regs);
      }
      ctx.asmgen.Jmp(label_cond);
      ctx.asmgen.Output() << label_loop << ": # loop body\n";
      GenerateAsm(ctx, node->lhs, dest, free_calc_regs);
      if (node->rhs) {
        ctx.asmgen.Output() << label_next << ": # update\n";
        GenerateAsm(ctx, node->rhs->next, dest, free_calc_regs);
      }
      ctx.asmgen.Output() << label_cond << ": # condition\n";
      GenerateAsm(ctx, node->cond, dest, free_calc_regs);
      ctx.asmgen.JmpIfNotZero(dest, label_loop);
    }
    return;
  case Node::kCall:
    {
      SetErshovNumber(ctx.src, node);
      const int num_arg = CountListItems(node->rhs);

      vector<Asm::Register> saved_regs;
      auto save_reg = [&](Asm::Register reg) {
        ctx.asmgen.Push64(reg);
        saved_regs.push_back(reg);
      };

      // 戻り値、引数レジスタを退避
      if (dest != Asm::kRegA) {
        save_reg(Asm::kRegA);
      }
      for (int i = 0; i < num_arg; ++i) {
        auto reg = static_cast<Asm::Register>(Asm::kRegV0 + i);
        if (reg != dest && !free_calc_regs.test(reg)) {
          save_reg(reg);
          free_calc_regs.set(reg);
        }
      }

      // 関数名の評価結果を格納するレジスタを探す
      Asm::Register lhs_reg = Asm::kRegNV0;
      for (int i = Asm::kRegV0 + num_arg; i <= Asm::kRegY; ++i) {
        if (free_calc_regs.test(i)) {
          lhs_reg = static_cast<Asm::Register>(i);
          break;
        }
      }
      if (lhs_reg > Asm::kRegY) {
        lhs_reg = Asm::kRegY;
        save_reg(lhs_reg);
        free_calc_regs.set(lhs_reg);
      }

      // Ershov 数が 2 以上の引数を事前に評価し、スタックに保存しておく
      vector<Node*> args;
      for (auto arg = node->rhs; arg; arg = arg->next) {
        args.push_back(arg);
        if (arg->ershov >= 2) {
          GenerateAsm(ctx, arg, dest, free_calc_regs);
          ctx.asmgen.Push64(dest);
        }
      }

      GenerateAsm(ctx, node->lhs, lhs_reg, free_calc_regs);
      free_calc_regs.reset(lhs_reg);

      // 引数レジスタに実引数を設定する
      for (int i = num_arg - 1; i >= 0; --i) {
        auto arg_reg = static_cast<Asm::Register>(Asm::kRegV0 + i);
        if (args[i]->ershov == 1) {
          GenerateAsm(ctx, args[i], arg_reg, free_calc_regs);
        } else {
          ctx.asmgen.Pop64(arg_reg);
        }
      }

      // 関数を呼び、結果を dest レジスタにコピーする
      ctx.asmgen.Output() << "    # calling " << node->lhs->token->raw << '\n';
      ctx.asmgen.Call(lhs_reg);
      if (Asm::kRegA != dest) {
        ctx.asmgen.Mov64(dest, Asm::kRegA);
      }

      // 退避したレジスタの復帰
      while (!saved_regs.empty()) {
        ctx.asmgen.Pop64(saved_regs.back());
        saved_regs.pop_back();
      }
    }
    return;
  case Node::kStr:
    comment_node();
    ctx.asmgen.LoadLabelAddr(
        dest, StringLabel(get<StringIndex>(node->value).i));
    return;
  case Node::kExtern:
  case Node::kTypedef:
    return;
  case Node::kSizeof:
    comment_node();
    ctx.asmgen.Mov64(dest, SizeofType(ctx.src, node->lhs->type));
    return;
  case Node::kCast:
    GenerateAsm(ctx, node->lhs, dest, free_calc_regs, lval);
    if (auto t = GetUserBaseType(node->rhs->type); IsIntegral(t)) {
      if (auto bits = get<long>(t->value); bits < 64) {
        ctx.asmgen.And64(dest, (1 << get<long>(t->value)) - 1);
      }
    } else {
      cerr << "not implemented cast for " << t << endl;
      ErrorAt(ctx.src, *node->token);
    }
    return;
  default:
    ; // pass
  }

  // ここから Expression に対する処理
  SetErshovNumber(ctx.src, node);

  const bool request_lval =
    node->kind == Node::kAssign ||
    node->kind == Node::kDefVar ||
    node->kind == Node::kAddr
    ;

  Asm::Register reg;
  const bool lhs_in_dest = node->rhs == nullptr ||
                           node->lhs->ershov >= node->rhs->ershov;
  if (lhs_in_dest) {
    GenerateAsm(ctx, node->lhs, dest, free_calc_regs, request_lval);
    if (node->rhs) {
      reg = UseAnyCalcReg(free_calc_regs);
      GenerateAsm(ctx, node->rhs, reg, free_calc_regs);
    }
  } else {
    GenerateAsm(ctx, node->rhs, dest, free_calc_regs);
    reg = UseAnyCalcReg(free_calc_regs);
    GenerateAsm(ctx, node->lhs, reg, free_calc_regs, request_lval);
  }
  auto lhs_reg = lhs_in_dest ? dest : reg;
  auto rhs_reg = lhs_in_dest ? reg : dest;

  comment_node();

  switch (node->kind) {
  case Node::kAdd:
    ctx.asmgen.Add64(dest, reg);
    break;
  case Node::kSub:
    if (lhs_in_dest) {
      ctx.asmgen.Sub64(dest, reg);
    } else {
      ctx.asmgen.Sub64(reg, dest);
      ctx.asmgen.Mov64(dest, reg);
    }
    break;
  case Node::kMul:
    ctx.asmgen.Mul64(dest, reg);
    break;
  case Node::kDiv:
    if (lhs_in_dest) {
      ctx.asmgen.Div64(dest, reg);
    } else {
      ctx.asmgen.Div64(reg, dest);
      ctx.asmgen.Mov64(dest, reg);
    }
    break;
  case Node::kEqu:
    ctx.asmgen.CmpSet(Asm::kCmpE, dest, dest, reg);
    break;
  case Node::kNEqu:
    ctx.asmgen.CmpSet(Asm::kCmpNE, dest, dest, reg);
    break;
  case Node::kGT:
    ctx.asmgen.CmpSet(Asm::kCmpG, dest, lhs_reg, rhs_reg);
    break;
  case Node::kLE:
    ctx.asmgen.CmpSet(Asm::kCmpLE, dest, lhs_reg, rhs_reg);
    break;
  case Node::kAssign:
    ctx.asmgen.Store64(lhs_reg, 0, rhs_reg);
    if (lval && !lhs_in_dest) {
      ctx.asmgen.Mov64(dest, reg);
    } else if (!lval && lhs_in_dest) {
      ctx.asmgen.Mov64(dest, reg);
    }
    break;
  case Node::kAddr:
    break;
  case Node::kDeref:
    if (!lval) {
      ctx.asmgen.Load64(dest, dest, 0);
    }
    break;
  default:
    cerr << "should not come here" << endl;
    ErrorAt(ctx.src, *node->token);
  }

  if (auto t = GetUserBaseType(node->type); IsIntegral(t)) {
    if (auto bits = get<long>(t->value); bits < 64) {
      ctx.asmgen.And64(dest, (1 << bits) - 1);
    }
  }
}

void PrintDebugInfo(Node* ast, vector<opela_type::String>& strings) {
  PrintASTRec(cout, ast);
  cout << '\n';
  for (size_t i = 0; i < strings.size(); ++i) {
    cout << StringLabel(i) << ": \"";
    for (auto ch : strings[i]) {
      cout << static_cast<char>(ch);
    }
    cout << "\"\n";
  }
}

int main(int argc, char** argv) {
  if (int err = ParseArgs(argc, argv)) {
    return err;
  }

  Asm* asmgen;
  if (target_arch == "x86_64") {
    asmgen = NewAsm(AsmArch::kX86_64, cout);
  } else {
    cerr << "current version doesn't support " << target_arch << endl;
    return 1;
  }

  Source src;
  src.ReadAll(cin);
  Tokenizer tokenizer(src);
  TypeManager type_manager(src);
  Scope scope;
  std::vector<opela_type::String> strings;
  list<Type*> unresolved_types;
  list<Node*> undeclared_ids;
  ASTContext ast_ctx{src, tokenizer, type_manager, scope, strings,
                     unresolved_types, undeclared_ids, nullptr};
  auto ast = Program(ast_ctx);

  if (verbosity >= 1) {
    cout << "/* AST before resolving types\n";
    PrintDebugInfo(ast, strings);
    cout << "*/\n\n";
  }
  ResolveIDs(ast_ctx);
  ResolveType(ast_ctx);
  SetTypeProgram(ast_ctx, ast);
  cout << "/* AST\n";
  PrintDebugInfo(ast, strings);
  cout << "*/\n\n";

  Asm::RegSet free_calc_regs;
  if (!asmgen->SameReg(Asm::kRegA, Asm::kRegV0)) {
    free_calc_regs.set(Asm::kRegV0);
  }
  free_calc_regs.set(Asm::kRegV1);
  free_calc_regs.set(Asm::kRegV2);
  free_calc_regs.set(Asm::kRegV3);
  free_calc_regs.set(Asm::kRegV4);
  free_calc_regs.set(Asm::kRegV5);
  free_calc_regs.set(Asm::kRegX);
  free_calc_regs.set(Asm::kRegY);

  auto globals = scope.GetGlobals();
  cout << ".intel_syntax noprefix\n";
  for (auto obj : globals) {
    if (obj->linkage == Object::kGlobal && obj->kind == Object::kFunc) {
      GenContext ctx{src, *asmgen, obj};
      GenerateAsm(ctx, obj->def, Asm::kRegA, free_calc_regs);
    }
  }

  asmgen->Output() << ".global _init_opela\n_init_opela:\n";
  asmgen->Push64(Asm::kRegBP);
  asmgen->Mov64(Asm::kRegBP, Asm::kRegSP);
  for (auto obj : globals) {
    if (obj->linkage == Object::kGlobal && obj->kind == Object::kVar) {
      auto var_def = obj->def;
      if (var_def->rhs && var_def->rhs->kind != Node::kInt) {
        GenContext ctx{src, *asmgen, obj};
        GenerateAsm(ctx, var_def->rhs, Asm::kRegA, free_calc_regs);
        ctx.asmgen.Store64(var_def->lhs->token->raw, Asm::kRegA);
      }
    }
  }
  asmgen->Output() << "_init_opela.exit:\n";
  asmgen->Leave();
  asmgen->Ret();

  asmgen->Output() << ".section .init_array\n";
  asmgen->Output() << "    .dc.a _init_opela\n";

  asmgen->Output() << ".section .data\n";
  for (size_t i = 0; i < strings.size(); ++i) {
    cout << StringLabel(i) << ":\n    .byte ";
    for (auto ch : strings[i]) {
      cout << static_cast<int>(ch) << ',';
    }
    cout << "0\n";
  }

  for (auto obj : globals) {
    if (obj->linkage == Object::kGlobal && obj->kind == Object::kVar) {
      auto var_def = obj->def;
      auto obj_size = SizeofType(src, obj->type);
      asmgen->Output() << obj->id->raw << ": ";
      if (var_def->rhs && var_def->rhs->kind == Node::kInt) {
        asmgen->Output() << kSizeMap[obj_size] << ' '
                         << get<opela_type::Int>(var_def->rhs->value) << '\n';
      } else {
        asmgen->Output() << ".zero " << obj_size << '\n';
      }
    }
  }
}
