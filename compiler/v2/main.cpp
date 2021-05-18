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

void GenerateAsm(Source& src, Asm* asmgen, Node* node,
                 Asm::Register dest, Asm::RegSet free_calc_regs) {
  SetErshovNumber(src, node);

  if (node->kind == Node::kInt) {
    asmgen->Mov64(dest, get<opela_type::Int>(node->value));
    return;
  }

  Asm::Register reg;
  const bool lhs_in_dest = node->lhs->ershov >= node->rhs->ershov;
  if (lhs_in_dest) {
    GenerateAsm(src, asmgen, node->lhs, dest, free_calc_regs);
    reg = UseAnyCalcReg(free_calc_regs);
    GenerateAsm(src, asmgen, node->rhs, reg, free_calc_regs);
  } else {
    GenerateAsm(src, asmgen, node->rhs, dest, free_calc_regs);
    reg = UseAnyCalcReg(free_calc_regs);
    GenerateAsm(src, asmgen, node->lhs, reg, free_calc_regs);
  }

  switch (node->kind) {
  case Node::kAdd:
    asmgen->Add64(dest, reg);
    break;
  case Node::kSub:
    if (lhs_in_dest) {
      asmgen->Sub64(dest, reg);
    } else {
      asmgen->Sub64(reg, dest);
      asmgen->Mov64(dest, reg);
    }
    break;
  case Node::kMul:
    asmgen->Mul64(dest, reg);
    break;
  case Node::kDiv:
    if (lhs_in_dest) {
      asmgen->Div64(dest, reg);
    } else {
      asmgen->Div64(reg, dest);
      asmgen->Mov64(dest, reg);
    }
    break;
  default:
    cerr << "should not come here" << endl;
    ErrorAt(src, *node->token);
  }
}

int main(int argc, char** argv) {
  Source src;
  src.ReadAll(cin);
  Tokenizer tokenizer(src);
  auto ast = Expression(tokenizer);
  PrintAST(cerr, ast);

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
  cout << ".intel_syntax noprefix\n"
          ".global main\n"
          "main:\n";
  GenerateAsm(src, asmgen, ast, Asm::kRegA, free_calc_regs);
  cout << "    ret\n";
}
