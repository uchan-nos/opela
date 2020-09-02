#include "ast.hpp"

#include "token.hpp"

using namespace std;

Node* MakeNode(Node::Kind kind, Node* lhs, Node* rhs) {
  return new Node{kind, lhs, rhs, 0};
}

Node* NewNodeInt(std::int64_t value) {
  return new Node{Node::kInt, nullptr, nullptr, value};
}

Node* Expr() {
  return Equality();
}

Node* Equality() {
  auto node{Relational()};

  for (;;) {
    if (Consume("==")) {
      node = new Node{Node::kEqu, node, Relational(), 0};
    } else if (Consume("!=")) {
      node = new Node{Node::kNEqu, node, Relational(), 0};
    } else {
      return node;
    }
  }
}

Node* Relational() {
  auto node{Additive()};

  for (;;) {
    if (Consume("<")) {
      node = new Node{Node::kLT, node, Additive(), 0};
    } else if (Consume("<=")) {
      node = new Node{Node::kLE, node, Additive(), 0};
    } else if (Consume(">")) {
      node = new Node{Node::kLT, Additive(), node, 0};
    } else if (Consume(">=")) {
      node = new Node{Node::kLE, Additive(), node, 0};
    } else {
      return node;
    }
  }
}

Node* Additive() {
  auto node{Multiplicative()};

  for (;;) {
    if (Consume("+")) {
      node = new Node{Node::kAdd, node, Multiplicative(), 0};
    } else if (Consume("-")) {
      node = new Node{Node::kSub, node, Multiplicative(), 0};
    } else {
      return node;
    }
  }
}

Node* Multiplicative() {
  auto node{Unary()};

  for (;;) {
    if (Consume("*")) {
      node = new Node{Node::kMul, node, Unary(), 0};
    } else if (Consume("/")) {
      node = new Node{Node::kDiv, node, Unary(), 0};
    } else {
      return node;
    }
  }
}

Node* Unary() {
  if (Consume("+")) {
    return Primary();
  } else if (Consume("-")) {
    auto zero{new Node{Node::kInt, 0, 0, 0}};
    return new Node{Node::kSub, zero, Primary(), 0};
  }
  return Primary();
}

Node* Primary() {
  if (Consume("(")) {
    auto node{Expr()};
    Expect(")");
    return node;
  }

  return new Node{Node::kInt, 0, 0, Expect(Token::kInt)->value};
}
