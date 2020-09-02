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
  auto node{Mul()};

  for (;;) {
    if (Consume(Token::kOp, "+")) {
      node = new Node{Node::kAdd, node, Mul(), 0};
    } else if (Consume(Token::kOp, "-")) {
      node = new Node{Node::kSub, node, Mul(), 0};
    } else {
      return node;
    }
  }
}

Node* Mul() {
  auto node{Unary()};

  for (;;) {
    if (Consume(Token::kOp, "*")) {
      node = new Node{Node::kMul, node, Unary(), 0};
    } else if (Consume(Token::kOp, "/")) {
      node = new Node{Node::kDiv, node, Unary(), 0};
    } else {
      return node;
    }
  }
}

Node* Unary() {
  if (Consume(Token::kOp, "+")) {
    return Primary();
  } else if (Consume(Token::kOp, "-")) {
    auto zero{new Node{Node::kInt, 0, 0, 0}};
    return new Node{Node::kSub, zero, Primary(), 0};
  }
  return Primary();
}

Node* Primary() {
  if (Consume(Token::kLParen)) {
    auto node{Expr()};
    Expect(Token::kRParen);
    return node;
  }

  return new Node{Node::kInt, 0, 0, Expect(Token::kInt)->value};
}

