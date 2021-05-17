#pragma once

#include <ostream>
#include <variant>
#include <vector>

#include "token.hpp"
#include "types.hpp"

struct Node {
  enum Kind {
    kInt, // 整数リテラル
    kAdd, // 2項演算子 +
    kSub, // 2項演算子 -
    kMul, // 2項演算子 *
    kDiv, // 2項演算子 /
  } kind;

  Token* token; // このノードを代表するトークン

  // 子ノード
  Node* lhs;
  Node* rhs;

  std::variant<opela_type::Int> value;
  int ershov = 0;
};

Node* Expression(Tokenizer& t);
Node* Additive(Tokenizer& t);
Node* Multiplicative(Tokenizer& t);
Node* Primary(Tokenizer& t);

void PrintAST(std::ostream& os, Node* ast);
