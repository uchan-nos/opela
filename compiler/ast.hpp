#pragma once

#include <cstdint>
#include <string>

#include "token.hpp"

struct LVar {
  Token* token;
  std::int64_t offset;
};

struct Node {
  enum Kind {
    kAdd,
    kSub,
    kMul,
    kDiv,
    kInt,
    kEqu,
    kNEqu,
    kLT,
    kLE,
    kLVar, // local variable
    kRet,
    kIf,
    kAssign,
    kLoop, // 無限ループ
    kFor, // 条件付きループ
    kBlock, // 複文
    kCall,
    kEList, // 式リスト
    kDeclSeq,
    kDefFunc,
  } kind;

  Token* token; // このノードを代表するトークン
  Node* next; // 複文中で次の文を表す
  Node* cond; // 条件式がある文における条件式
  Node* lhs;
  Node* rhs;

  union {
    std::int64_t i;
    LVar* lvar;
  } value;
};

size_t LVarBytes();

Node* Program();
Node* DeclarationSequence();
Node* FunctionDefinition();
Node* Statement();
Node* CompoundStatement();
Node* SelectionStatement();
Node* IterationStatement();
Node* JumpStatement();
Node* ExpressionStatement();
Node* Expr();
Node* Assignment();
Node* Equality();
Node* Relational();
Node* Additive();
Node* Multiplicative();
Node* Unary();
Node* Postfix();
Node* Primary();
