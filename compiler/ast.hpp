#pragma once

#include <cstdint>

struct Node {
  enum Kind {
    kAdd,
    kSub,
    kMul,
    kDiv,
    kInt,
  } kind;

  Node* lhs;
  Node* rhs;
  std::int64_t value;
};

Node* MakeNode(Node::Kind kind, const Node*& lhs, const Node*& rhs);
Node* MakeNodeInt(std::int64_t value);

Node* Expr();
Node* Mul();
Node* Primary();
