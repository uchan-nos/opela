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
    kDefLVar,
  } kind;

  Node* next;
  Node* lhs;
  Node* rhs;

  union {
    std::int64_t i;
    LVar* lvar;
  } value;
};

size_t LVarBytes();

Node* Program();
Node* Statement();
Node* Expr();
Node* Equality();
Node* Relational();
Node* Additive();
Node* Multiplicative();
Node* Unary();
Node* Primary();
