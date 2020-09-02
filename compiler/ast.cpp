#include "ast.hpp"

#include "token.hpp"

using namespace std;

Node* NewNodeExpr(Node::Kind kind, Node* lhs, Node* rhs) {
  return new Node{kind, nullptr, lhs, rhs, 0};
}

Node* NewNodeInt(std::int64_t value) {
  return new Node{Node::kInt, nullptr, nullptr, nullptr, value};
}

Node* Program() {
  if (AtEOF()) {
    return nullptr;
  }

  auto head{Statement()};
  auto cur{head};
  while (!AtEOF()) {
    cur->next = Statement();
    cur = cur->next;
  }
  return head;
}

Node* Statement() {
  auto node{Expr()};
  Expect(";");
  return node;
}

Node* Expr() {
  return Equality();
}

Node* Equality() {
  auto node{Relational()};

  for (;;) {
    if (Consume("==")) {
      node = NewNodeExpr(Node::kEqu, node, Relational());
    } else if (Consume("!=")) {
      node = NewNodeExpr(Node::kNEqu, node, Relational());
    } else {
      return node;
    }
  }
}

Node* Relational() {
  auto node{Additive()};

  for (;;) {
    if (Consume("<")) {
      node = NewNodeExpr(Node::kLT, node, Additive());
    } else if (Consume("<=")) {
      node = NewNodeExpr(Node::kLE, node, Additive());
    } else if (Consume(">")) {
      node = NewNodeExpr(Node::kLT, Additive(), node);
    } else if (Consume(">=")) {
      node = NewNodeExpr(Node::kLE, Additive(), node);
    } else {
      return node;
    }
  }
}

Node* Additive() {
  auto node{Multiplicative()};

  for (;;) {
    if (Consume("+")) {
      node = NewNodeExpr(Node::kAdd, node, Multiplicative());
    } else if (Consume("-")) {
      node = NewNodeExpr(Node::kSub, node, Multiplicative());
    } else {
      return node;
    }
  }
}

Node* Multiplicative() {
  auto node{Unary()};

  for (;;) {
    if (Consume("*")) {
      node = NewNodeExpr(Node::kMul, node, Unary());
    } else if (Consume("/")) {
      node = NewNodeExpr(Node::kDiv, node, Unary());
    } else {
      return node;
    }
  }
}

Node* Unary() {
  if (Consume("+")) {
    return Primary();
  } else if (Consume("-")) {
    auto zero{NewNodeInt(0)};
    return NewNodeExpr(Node::kSub, zero, Primary());
  }
  return Primary();
}

Node* Primary() {
  if (Consume("(")) {
    auto node{Expr()};
    Expect(")");
    return node;
  }

  return NewNodeInt(Expect(Token::kInt)->value);
}
