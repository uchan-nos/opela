#include "ast.hpp"

#include <map>

using namespace std;

namespace {

Node* NewNodeExpr(Node::Kind kind, Node* lhs, Node* rhs) {
  return new Node{kind, nullptr, lhs, rhs, {0}};
}

Node* NewNodeInt(std::int64_t value) {
  return new Node{Node::kInt, nullptr, nullptr, nullptr, {value}};
}

map<string, LVar*> local_vars;
Node* NewNodeLVar(Token* tk) {
  auto node = new Node{Node::kLVar, nullptr, nullptr, nullptr, {0}};
  string name(tk->loc, tk->len);

  if (auto it = local_vars.find(name); it != local_vars.end()) {
    node->value.lvar = it->second;
  } else {
    node->value.lvar = new LVar{tk, 0};
  }
  return node;
}

} // namespace

size_t LVarBytes() {
  return local_vars.size() * 8;
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
  if (Consume(";")) {
    return node;
  } else if (Consume(":=")) {
    auto tk{node->value.lvar->token};
    if (node->kind != Node::kLVar || node->value.lvar->offset != 0) {
      Error(*tk);
    }

    auto init{Expr()};
    Expect(";");
    local_vars[string(tk->loc, tk->len)] = node->value.lvar;
    node->value.lvar->offset = local_vars.size() * 8;
    return new Node{Node::kDefLVar, nullptr, node, init, {0}};
  }
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
  } else if (auto tk = Consume(Token::kId)) {
    return NewNodeLVar(tk);
  }

  return NewNodeInt(Expect(Token::kInt)->value);
}
