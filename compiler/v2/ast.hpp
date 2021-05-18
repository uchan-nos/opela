#pragma once

#include <ostream>
#include <variant>
#include <vector>

#include "token.hpp"
#include "types.hpp"

struct Node {
  enum Kind {
    kInt,   // 整数リテラル
    kAdd,   // 2項演算子 +
    kSub,   // 2項演算子 -
    kMul,   // 2項演算子 *
    kDiv,   // 2項演算子 /
    kEqu,   // 2項演算子 ==
    kNEqu,  // 2項演算子 !=
    kGT,    // 2項演算子 >
    kLE,    // 2項演算子 <=
    kBlock, // 複文
  } kind;

  Token* token; // このノードを代表するトークン

  // 子ノード
  Node* next = nullptr; // 複文において次の文を指す
  Node* lhs = nullptr;
  Node* rhs = nullptr;

  std::variant<opela_type::Int> value = {};
  int ershov = 0;
};

Node* Program(Tokenizer& t);
Node* CompoundStatement(Tokenizer& t);
Node* ExpressionStatement(Tokenizer& t);
Node* Expression(Tokenizer& t);
Node* Equality(Tokenizer& t);
Node* Relational(Tokenizer& t);
Node* Additive(Tokenizer& t);
Node* Multiplicative(Tokenizer& t);
Node* Unary(Tokenizer& t);
Node* Primary(Tokenizer& t);

void PrintAST(std::ostream& os, Node* ast);
void PrintASTRec(std::ostream& os, Node* ast);
