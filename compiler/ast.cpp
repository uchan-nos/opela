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

Node* Statement() {
  if (Peek(Token::kRet)) {
    return JumpStatement();
  }

  if (Peek(Token::kIf)) {
    return SelectionStatement();
  }

  if (Peek(Token::kFor)) {
    return IterationStatement();
  }

  if (Peek("{")) {
    return CompoundStatement();
  }

  return ExpressionStatement();
}

Node* CompoundStatement() {
  auto start{Expect("{")};
  if (Consume("}")) {
    return new Node{Node::kInt, nullptr, nullptr, nullptr, nullptr, {0}};
  }

  auto head{new Node{Node::kBlock, start, nullptr, nullptr, nullptr, {0}}};
  auto cur{head};
  while (!Consume("}")) {
    cur->next = Statement();
    cur = cur->next;
  }
  return head;
}

Node* SelectionStatement() {
  auto tk{Expect(Token::kIf)};
  auto expr{Expr()};
  auto body{CompoundStatement()};
  Node* body_else{nullptr};
  if (Consume(Token::kElse)) {
    body_else = Statement();
  }
  return new Node{Node::kIf, tk, body_else, expr, body, {0}};
}

Node* IterationStatement() {
  auto tk{Expect(Token::kFor)};
  if (Peek("{")) {
    auto body{CompoundStatement()};
    return new Node{Node::kLoop, tk, nullptr, nullptr, body, {0}};
  }

  auto expr{Expr()};
  if (Consume(";")) {
    auto cond{Expr()};
    cond->next = expr; // condition -> initialization
    Expect(";");
    auto succ{Expr()};
    expr->next = succ; // initialization -> successor
    expr = cond;
  }
  auto body{CompoundStatement()};
  return new Node{Node::kFor, tk, nullptr, expr, body, {0}};
}

Node* JumpStatement() {
  auto tk{Expect(Token::kRet)};
  auto expr{Expr()};
  Expect(";");
  return new Node{Node::kRet, tk, nullptr, expr, nullptr, {0}};
}

Node* ExpressionStatement() {
  auto node{Expr()};
  Expect(";");
  return node;
}

Node* Expr() {
  return Assignment();
}

Node* Assignment() {
  auto node{Equality()};

  // 代入演算は右結合
  if (auto op{Consume("=")}) {
    node = NewNodeExpr(Node::kAssign, op, node, Assignment());
  } else if (auto op{Consume(":=")}) {
    if (node->kind != Node::kLVar) {
      cerr << "lhs of ':=' must be an identifier" << endl;
      ErrorAt(node->token->loc);
    } else if (node->value.lvar->offset != 0) {
      cerr << "'" << node->token->Raw() << "' is redefined" << endl;
      ErrorAt(node->token->loc);
    }

    local_vars[node->token->Raw()] = node->value.lvar;
    node->value.lvar->offset = local_vars.size() * 8;
    node = new Node{Node::kAssign, op, nullptr, node, Assignment(), {0}};
  }
  return node;
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
