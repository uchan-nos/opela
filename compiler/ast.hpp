#pragma once

#include <cstdint>

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
  } kind;

  Node* lhs;
  Node* rhs;
  std::int64_t value;
};

Node* MakeNode(Node::Kind kind, const Node*& lhs, const Node*& rhs);
Node* MakeNodeInt(std::int64_t value);

Node* Expr();
Node* Equality();
Node* Relational();
Node* Additive();
Node* Multiplicative();
Node* Unary();
Node* Primary();
