#include "ast.hpp"

#include <iostream>

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

Node* NewNodeLVar(Context* ctx, Token* tk) {
  auto node{NewNode(Node::kLVar, tk)};
  if (auto it = ctx->local_vars.find(tk->Raw()); it != ctx->local_vars.end()) {
    node->value.lvar = it->second;
  } else {
    node->value.lvar = new LVar{ctx, tk, 0};
  }
  return node;
}

void AllocateLVar(Context* ctx, LVar* lvar) {
  ctx->local_vars[lvar->token->Raw()] = lvar;
  lvar->offset = ctx->StackSize();
}

void ErrorRedefineLVar(LVar* lvar) {
  if (lvar->offset != 0) {
    cerr << "'" << lvar->token->Raw() << "' is redefined" << endl;
    ErrorAt(lvar->token->loc);
  }
}

Context* cur_ctx; // コンパイル中の文や式を含む関数のコンテキスト

const map<Node::Kind, const char*> kUnaryOps{
  {Node::kAddr,  "&"},
  {Node::kDeref, "*"},
};

} // namespace

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

  auto func_name{name->Raw()};
  cur_ctx = new Context{func_name, {}, {}};
  contexts[func_name] = cur_ctx;

  Expect("(");
  if (!Consume(")")) {
    for (;;) {
      auto lvar{new LVar{cur_ctx, Expect(Token::kId), 0}};
      cur_ctx->params.push_back(lvar);
      AllocateLVar(cur_ctx, lvar);
      if (!Consume(",")) {
        Expect(")");
        break;
      }
    }
  }

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

  if (Consume(Token::kVar)) {
    auto id{Expect(Token::kId)};
    auto tspec{TypeSpecifier()};
    Node* init{nullptr};
    if (Consume("=")) {
      init = Expr();
    }
    Expect(";");

    auto node{NewNodeLVar(cur_ctx, id)};
    ErrorRedefineLVar(node->value.lvar);
    AllocateLVar(cur_ctx, node->value.lvar);
    return new Node{Node::kDefVar, id, nullptr, tspec, node, init, {0}};
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
    }
    ErrorRedefineLVar(node->value.lvar);

    AllocateLVar(cur_ctx, node->value.lvar);
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
    return Unary();
  } else if (auto op{Consume("-")}) {
    auto zero{NewNodeInt(nullptr, 0)};
    return NewNodeExpr(Node::kSub, op, zero, Unary());
  }

  for (auto [ k, v ] : kUnaryOps) {
    if (auto op{Consume(v)}) {
      return NewNodeExpr(k, op, Unary(), nullptr);
    }
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
    return NewNodeLVar(cur_ctx, tk);
  }

  auto tk{Expect(Token::kInt)};
  return NewNodeInt(tk, tk->value);
}

Node* TypeSpecifier() {
  int64_t num_ptr{0};
  while (Consume("*")) {
    ++num_ptr;
  }
  auto type{Expect(Token::kId)};
  auto node{NewNode(Node::kType, type)};
  node->value.i = num_ptr;
  return node;
}
