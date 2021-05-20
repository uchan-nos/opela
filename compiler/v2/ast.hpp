#pragma once

#include <ostream>
#include <variant>
#include <vector>

#include "object.hpp"
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
    kId,    // 識別子（変数、関数、型）
    kDefVar,// 変数定義
  } kind;

  Token* token; // このノードを代表するトークン

  // 子ノード
  Node* lhs = nullptr;
  Node* rhs = nullptr;
  Node* next = nullptr; // 複文において次の文を指す

  std::variant<opela_type::Int, Object*> value = {};
  int ershov = 0;
};

struct ASTContext {
  Tokenizer& t;
  Scope& sc;
  std::vector<Object*>& locals;
};

Node* Program(Tokenizer& t);
Node* CompoundStatement(ASTContext& ctx);
Node* ExpressionStatement(ASTContext& ctx);
Node* Expression(ASTContext& ctx);
Node* Assignment(ASTContext& ctx);
Node* Equality(ASTContext& ctx);
Node* Relational(ASTContext& ctx);
Node* Additive(ASTContext& ctx);
Node* Multiplicative(ASTContext& ctx);
Node* Unary(ASTContext& ctx);
Node* Primary(ASTContext& ctx);

void PrintAST(std::ostream& os, Node* ast);
void PrintASTRec(std::ostream& os, Node* ast);
