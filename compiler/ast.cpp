#include "ast.hpp"

#include <iostream>
#include <map>

#include "source.hpp"

using namespace std;

namespace {

Node* NewNode(Node::Kind kind, Token* tk) {
  return new Node{kind, tk, nullptr, nullptr, nullptr, nullptr, {0}};
}

Node* NewNodeExpr(Node::Kind kind, Token* op, Node* lhs, Node* rhs) {
  return new Node{kind, op, nullptr, nullptr, lhs, rhs, {0}};
}

Node* NewNodeInt(Token* tk, int64_t value) {
  return new Node{Node::kInt, tk, nullptr, nullptr, nullptr, nullptr, {value}};
}

map<string, LVar*> local_vars;
Node* NewNodeLVar(Token* tk) {
  auto node{NewNode(Node::kLVar, tk)};
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
  return DeclarationSequence();
}

Node* DeclarationSequence() {
  auto head{NewNode(Node::kDeclSeq, nullptr)};
  auto cur{head};
  for (;;) {
    if (Peek(Token::kFunc)) {
      cur->next = FunctionDefinition();
    } else {
      return head;
    }
    cur = cur->next;
  }
}

Node* FunctionDefinition() {
  Expect(Token::kFunc);
  auto name{Expect(Token::kId)};
  Expect("(");
  Expect(")");
  auto body{CompoundStatement()};
  return new Node{Node::kDefFunc, name, nullptr, nullptr, body, nullptr, {0}};
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
  auto head{NewNode(Node::kBlock, Expect("{"))};
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
    if (Peek("{")) {
      body_else = CompoundStatement();
    } else if (Peek(Token::kIf)) {
      body_else = SelectionStatement();
    } else {
      cerr << "else must be followed by if/compound statement" << endl;
      ErrorAt(cur_token->loc);
    }
  }
  return new Node{Node::kIf, tk, nullptr, expr, body, body_else, {0}};
}

Node* IterationStatement() {
  auto tk{Expect(Token::kFor)};
  if (Peek("{")) {
    auto body{CompoundStatement()};
    return new Node{Node::kLoop, tk, nullptr, nullptr, body, nullptr, {0}};
  }

  auto cond{Expr()};
  Node* init{nullptr};
  if (Consume(";")) {
    init = cond;
    cond = Expr();
    Expect(";");
    init->next = Expr();
  }
  auto body{CompoundStatement()};
  return new Node{Node::kFor, tk, nullptr, cond, body, init, {0}};
}

Node* JumpStatement() {
  auto tk{Expect(Token::kRet)};
  auto expr{Expr()};
  Expect(";");
  return new Node{Node::kRet, tk, nullptr, nullptr, expr, nullptr, {0}};
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
    node = NewNodeExpr(Node::kAssign, op, node, Assignment());
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
  return Postfix();
}

Node* Postfix() {
  auto node{Primary()};

  for (;;) {
    if (auto op{Consume("(")}) {
      auto head{NewNode(Node::kEList, op)};
      auto cur{head};
      if (!Consume(")")) {
        for (;;) {
          cur->next = Expr();
          cur = cur->next;
          if (!Consume(",")) {
            Expect(")");
            break;
          }
        }
      }
      node = NewNodeExpr(Node::kCall, op, node, head);
    } else {
      return node;
    }
  }
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
