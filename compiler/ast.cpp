#include "ast.hpp"

#include <iostream>
#include <map>

#include "source.hpp"

using namespace std;

namespace {

Node* NewNodeExpr(Node::Kind kind, Token* op, Node* lhs, Node* rhs) {
  return new Node{kind, op, nullptr, lhs, rhs, {0}};
}

Node* NewNodeInt(Token* tk, int64_t value) {
  return new Node{Node::kInt, tk, nullptr, nullptr, nullptr, {value}};
}

map<string, LVar*> local_vars;
Node* NewNodeLVar(Token* tk) {
  auto node = new Node{Node::kLVar, tk, nullptr, nullptr, nullptr, {0}};
  if (auto it = local_vars.find(tk->Raw()); it != local_vars.end()) {
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

Node* CompoundStatement() {
  Expect("{");
  if (Consume("}")) {
    return new Node{Node::kInt, nullptr, nullptr, nullptr, nullptr, {0}};
  }

  auto head{Statement()};
  auto cur{head};
  while (!Consume("}")) {
    cur->next = Statement();
    cur = cur->next;
  }
  return head;
}

Node* Statement() {
  if (auto tk{Consume(Token::kRet)}) {
    auto expr{Expr()};
    Expect(";");
    return new Node{Node::kRet, tk, nullptr, expr, nullptr, {0}};
  }

  if (auto tk{Consume(Token::kIf)}) {
    auto expr{Expr()};
    auto body{CompoundStatement()};
    return new Node{Node::kIf, tk, nullptr, expr, body, {0}};
  }

  auto node{Expr()};
  if (Consume(";")) {
    return node;
  } else if (auto op{Consume(":=")}) {
    if (node->kind != Node::kLVar) {
      cerr << "lhs of ':=' must be an identifier" << endl;
      ErrorAt(node->token->loc);
    }

    if (node->value.lvar->offset != 0) {
      cerr << "'" << node->token->Raw() << "' is redefined" << endl;
      ErrorAt(node->token->loc);
    }

    auto init{Expr()};
    Expect(";");
    local_vars[node->token->Raw()] = node->value.lvar;
    node->value.lvar->offset = local_vars.size() * 8;
    return new Node{Node::kDefLVar, op, nullptr, node, init, {0}};
  }
  return node;
}

Node* Expr() {
  return Equality();
}

Node* Equality() {
  auto node{Relational()};

  for (;;) {
    if (auto op{Consume("==")}) {
      node = NewNodeExpr(Node::kEqu, op, node, Relational());
    } else if (auto op{Consume("!=")}) {
      node = NewNodeExpr(Node::kNEqu, op, node, Relational());
    } else {
      return node;
    }
  }
}

Node* Relational() {
  auto node{Additive()};

  for (;;) {
    if (auto op{Consume("<")}) {
      node = NewNodeExpr(Node::kLT, op, node, Additive());
    } else if (auto op{Consume("<=")}) {
      node = NewNodeExpr(Node::kLE, op, node, Additive());
    } else if (auto op{Consume(">")}) {
      node = NewNodeExpr(Node::kLT, op, Additive(), node);
    } else if (auto op{Consume(">=")}) {
      node = NewNodeExpr(Node::kLE, op, Additive(), node);
    } else {
      return node;
    }
  }
}

Node* Additive() {
  auto node{Multiplicative()};

  for (;;) {
    if (auto op{Consume("+")}) {
      node = NewNodeExpr(Node::kAdd, op, node, Multiplicative());
    } else if (auto op{Consume("-")}) {
      node = NewNodeExpr(Node::kSub, op, node, Multiplicative());
    } else {
      return node;
    }
  }
}

Node* Multiplicative() {
  auto node{Unary()};

  for (;;) {
    if (auto op{Consume("*")}) {
      node = NewNodeExpr(Node::kMul, op, node, Unary());
    } else if (auto op{Consume("/")}) {
      node = NewNodeExpr(Node::kDiv, op, node, Unary());
    } else {
      return node;
    }
  }
}

Node* Unary() {
  if (Consume("+")) {
    return Primary();
  } else if (auto op{Consume("-")}) {
    auto zero{NewNodeInt(nullptr, 0)};
    return NewNodeExpr(Node::kSub, op, zero, Primary());
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

  auto tk{Expect(Token::kInt)};
  return NewNodeInt(tk, tk->value);
}
