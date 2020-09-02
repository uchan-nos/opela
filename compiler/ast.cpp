#include "ast.hpp"

#include "token.hpp"

using namespace std;

std::shared_ptr<Node> MakeNode(Node::Kind kind,
                               const std::shared_ptr<Node>& lhs,
                               const std::shared_ptr<Node>& rhs) {
  return make_shared<Node>(Node{kind, lhs, rhs, 0});
}

std::shared_ptr<Node> MakeNodeInt(std::int64_t value) {
  return make_shared<Node>(Node{Node::kInt, nullptr, nullptr, value});
}

shared_ptr<Node> Expr() {
  auto node{Mul()};

  for (;;) {
    if (Consume(Token::kOp, "+")) {
      node = MakeNode(Node::kAdd, node, Mul());
    } else if (Consume(Token::kOp, "-")) {
      node = MakeNode(Node::kSub, node, Mul());
    } else {
      return node;
    }
  }
}

shared_ptr<Node> Mul() {
  auto node{Primary()};

  for (;;) {
    if (Consume(Token::kOp, "*")) {
      node = MakeNode(Node::kMul, node, Primary());
    } else if (Consume(Token::kOp, "/")) {
      node = MakeNode(Node::kDiv, node, Primary());
    } else {
      return node;
    }
  }
}

shared_ptr<Node> Primary() {
  if (Consume(Token::kLParen)) {
    auto node{Expr()};
    Expect(Token::kRParen);
    return node;
  }

  return MakeNodeInt(Expect(Token::kInt)->value);
}

