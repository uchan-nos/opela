#include "ast.hpp"

#include <iostream>

#include "source.hpp"

using namespace std;

namespace {

Node* NewNode(Node::Kind kind, Token* tk) {
  return new Node{kind, tk, nullptr, nullptr, nullptr, nullptr,
                  {0}, {Type::kUndefined, 0}};
}

Node* NewNodeExpr(Node::Kind kind, Token* op, Node* lhs, Node* rhs, Type type) {
  return new Node{kind, op, nullptr, nullptr, lhs, rhs, {0}, type};
}

Node* NewNodeInt(Token* tk, int64_t value) {
  return new Node{Node::kInt, tk, nullptr, nullptr, nullptr, nullptr,
                  {value}, {Type::kInt, 0}};
}

Node* NewNodeLVar(Context* ctx, Token* tk, Type type) {
  auto node{NewNode(Node::kLVar, tk)};
  if (auto it = ctx->local_vars.find(tk->Raw()); it != ctx->local_vars.end()) {
    node->value.lvar = it->second;
  } else {
    node->value.lvar = new LVar{ctx, tk, 0, type};
  }
  node->type = node->value.lvar->type;
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

Type GetType(Node* type_spec) {
  if (type_spec->token->kind != Token::kId) {
    return {Type::kUndefined, 0};
  }

  Type type{Type::kUnknown, static_cast<unsigned int>(type_spec->value.i)};
  if (type_spec->token->Raw() == "int") {
    type.base = Type::kInt;
  }
  return type;
}

} // namespace

Node* Program() {
  auto node{DeclarationSequence()};
  Expect(Token::kEOF);
  return node;
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
      vector<Token*> params;
      do {
        auto param_name{Expect(Token::kId)};
        params.push_back(param_name);
      } while (Consume(","));
      auto type_spec{TypeSpecifier()};
      auto type{GetType(type_spec)};
      for (auto param_name : params) {
        auto lvar{new LVar{cur_ctx, param_name, 0, type}};
        cur_ctx->params.push_back(lvar);
        AllocateLVar(cur_ctx, lvar);
      }
      if (!Consume(",")) {
        Expect(")");
        break;
      }
    }
  }

  auto body{CompoundStatement()};
  auto node{NewNode(Node::kDefFunc, name)};
  node->lhs = body;
  return node;
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

    auto node{NewNodeLVar(cur_ctx, id, GetType(tspec))};
    ErrorRedefineLVar(node->value.lvar);
    AllocateLVar(cur_ctx, node->value.lvar);
    return new Node{Node::kDefVar, id, nullptr, tspec, node, init,
                    {0}, GetType(tspec)};
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
  return new Node{Node::kIf, tk, nullptr, expr, body, body_else,
                  {0}, {Type::kUndefined, 0}};
}

Node* IterationStatement() {
  auto tk{Expect(Token::kFor)};
  if (Peek("{")) {
    auto body{CompoundStatement()};
    auto node{NewNode(Node::kLoop, tk)};
    node->lhs = body;
    return node;
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
  return new Node{Node::kFor, tk, nullptr, cond, body, init,
                  {0}, {Type::kUndefined, 0}};
}

Node* JumpStatement() {
  auto tk{Expect(Token::kRet)};
  auto expr{Expr()};
  Expect(";");
  auto node{NewNode(Node::kRet, tk)};
  node->lhs = expr;
  return node;
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
    node = NewNodeExpr(Node::kAssign, op, node, Assignment(), node->type);
  } else if (auto op{Consume(":=")}) {
    if (node->kind != Node::kLVar) {
      cerr << "lhs of ':=' must be an identifier" << endl;
      ErrorAt(node->token->loc);
    }
    ErrorRedefineLVar(node->value.lvar);

    AllocateLVar(cur_ctx, node->value.lvar);
    auto init{Assignment()};
    node->value.lvar->type = init->type;
    node = NewNodeExpr(Node::kAssign, op, node, init, init->type);
  }
  return node;
}

Node* Equality() {
  auto node{Relational()};

  for (;;) {
    if (auto op{Consume("==")}) {
      node = NewNodeExpr(Node::kEqu, op, node, Relational(), {Type::kInt, 0});
    } else if (auto op{Consume("!=")}) {
      node = NewNodeExpr(Node::kNEqu, op, node, Relational(), {Type::kInt, 0});
    } else {
      return node;
    }
  }
}

Node* Relational() {
  auto node{Additive()};

  for (;;) {
    if (auto op{Consume("<")}) {
      node = NewNodeExpr(Node::kLT, op, node, Additive(), {Type::kInt, 0});
    } else if (auto op{Consume("<=")}) {
      node = NewNodeExpr(Node::kLE, op, node, Additive(), {Type::kInt, 0});
    } else if (auto op{Consume(">")}) {
      node = NewNodeExpr(Node::kLT, op, Additive(), node, {Type::kInt, 0});
    } else if (auto op{Consume(">=")}) {
      node = NewNodeExpr(Node::kLE, op, Additive(), node, {Type::kInt, 0});
    } else {
      return node;
    }
  }
}

Node* Additive() {
  auto node{Multiplicative()};

  for (;;) {
    // TODO merge both types of lhs and rhs
    if (auto op{Consume("+")}) {
      node = NewNodeExpr(Node::kAdd, op, node, Multiplicative(), node->type);
    } else if (auto op{Consume("-")}) {
      node = NewNodeExpr(Node::kSub, op, node, Multiplicative(), node->type);
    } else {
      return node;
    }
  }
}

Node* Multiplicative() {
  auto node{Unary()};

  for (;;) {
    // TODO merge both types of lhs and rhs
    if (auto op{Consume("*")}) {
      node = NewNodeExpr(Node::kMul, op, node, Unary(), node->type);
    } else if (auto op{Consume("/")}) {
      node = NewNodeExpr(Node::kDiv, op, node, Unary(), node->type);
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
    auto node{Unary()};
    return NewNodeExpr(Node::kSub, op, zero, node, node->type);
  }

  for (auto [ k, v ] : kUnaryOps) {
    if (auto op{Consume(v)}) {
      auto node{Unary()};
      auto type{node->type};
      if (k == Node::kDeref) {
        if (node->type.pointer == 0) {
          cerr << "try to dereference non-pointer" << endl;
          ErrorAt(node->token->loc);
        }
        --type.pointer;
      } else if (k == Node::kAddr) {
        ++type.pointer;
      }
      return NewNodeExpr(k, op, node, nullptr, type);
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
      // TODO: get the return type of a function
      node = NewNodeExpr(Node::kCall, op, node, head, {Type::kInt, 0});
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
    return NewNodeLVar(cur_ctx, tk, {Type::kUndefined, 0});
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
