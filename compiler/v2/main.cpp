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
  } else if (expr->lhs == nullptr && expr->rhs == nullptr) {
    return expr->ershov = 1;
  } else if (expr->lhs != nullptr && expr->rhs != nullptr) {
    return expr->ershov =
      SetErshovNumber(src, expr->lhs) + SetErshovNumber(src, expr->rhs);
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

void GenerateAsm(GenContext& ctx, Node* node,
                 Asm::Register dest, Asm::RegSet free_calc_regs) {
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
    ctx.asmgen.Load64(dest, Asm::kRegBP, get<Object*>(node->value)->bp_offset);
    return;
  case Node::kDefVar:
    {
      comment_node();
      GenerateAsm(ctx, node->rhs, dest, free_calc_regs);
      ctx.asmgen.Store64(Asm::kRegBP, get<Object*>(node->value)->bp_offset, dest);
      return;
    }
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

      ctx.asmgen.Push64(Asm::kRegBP);
      ctx.asmgen.Mov64(Asm::kRegBP, Asm::kRegSP);
      ctx.asmgen.Sub64(Asm::kRegSP, stack_size);
      ctx.asmgen.Xor64(dest, dest);
      GenerateAsm(func_ctx, node->lhs, dest, free_calc_regs);
      ctx.asmgen.Output() << ctx.func->id->raw << ".exit:\n";
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
  default:
    ; // pass
  }

  // ここから Expression に対する処理
  SetErshovNumber(ctx.src, node);

  Asm::Register reg;
  const bool lhs_in_dest = node->lhs->ershov >= node->rhs->ershov;
  if (lhs_in_dest) {
    GenerateAsm(ctx, node->lhs, dest, free_calc_regs);
    reg = UseAnyCalcReg(free_calc_regs);
    GenerateAsm(ctx, node->rhs, reg, free_calc_regs);
  } else {
    GenerateAsm(ctx, node->rhs, dest, free_calc_regs);
    reg = UseAnyCalcReg(free_calc_regs);
    GenerateAsm(ctx, node->lhs, reg, free_calc_regs);
  }

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
    if (lhs_in_dest) {
      ctx.asmgen.CmpSet(Asm::kCmpG, dest, dest, reg);
    } else {
      ctx.asmgen.CmpSet(Asm::kCmpG, dest, reg, dest);
    }
    break;
  case Node::kLE:
    if (lhs_in_dest) {
      ctx.asmgen.CmpSet(Asm::kCmpLE, dest, dest, reg);
    } else {
      ctx.asmgen.CmpSet(Asm::kCmpLE, dest, reg, dest);
    }
    break;
  default:
    cerr << "should not come here" << endl;
    ErrorAt(ctx.src, *node->token);
  }
}

int main(int argc, char** argv) {
  Source src;
  src.ReadAll(cin);
  Tokenizer tokenizer(src);
  auto ast = Program(src, tokenizer);

  cout << "/* AST\n";
  PrintASTRec(cout, ast);
  cout << "\n*/\n";

  Asm::RegSet free_calc_regs;
  free_calc_regs.set(Asm::kRegV0);
  free_calc_regs.set(Asm::kRegV1);
  free_calc_regs.set(Asm::kRegV2);
  free_calc_regs.set(Asm::kRegV3);
  free_calc_regs.set(Asm::kRegV4);
  free_calc_regs.set(Asm::kRegV5);
  free_calc_regs.set(Asm::kRegX);
  free_calc_regs.set(Asm::kRegY);

  auto asmgen = NewAsm(AsmArch::kX86_64, cout);

  GenContext ctx{src, *asmgen, get<Object*>(ast->value)};
  ctx.func->id = new Token{Token::kId, "main", {}};

  cout << ".intel_syntax noprefix\n"
          ".global main\n"
          "main:\n";
  GenerateAsm(ctx, ast, Asm::kRegA, free_calc_regs);
}
