#include "ast.hpp"

#include <map>
#include <iostream>
#include <sstream>
#include <string>

#include "magic_enum.hpp"
#include "object.hpp"

using namespace std;

namespace {

Node* NewNode(Node::Kind kind, Token* token) {
  return new Node{kind, token, nullptr, nullptr, nullptr, nullptr};
}

Node* NewNodeInt(Token* token, opela_type::Int value) {
  auto node = NewNode(Node::kInt, token);
  node->value = value;
  return node;
}

Node* NewNodeBinOp(Node::Kind kind, Token* op, Node* lhs, Node* rhs) {
  auto node = NewNode(kind, op);
  node->lhs = lhs;
  node->rhs = rhs;
  return node;
}

Node* NewNodeOneChild(Node::Kind kind, Token* token, Node* child) {
  auto node = NewNode(kind, token);
  node->lhs = child;
  return node;
}

Node* NewNodeCond(Node::Kind kind, Token* token,
                  Node* cond, Node* lhs, Node* rhs) {
  auto node = NewNodeBinOp(kind, token, lhs, rhs);
  node->cond = cond;
  return node;
}

map<Node*, size_t> node_number;
size_t NodeNo(Node* node) {
  if (auto it = node_number.find(node); it != node_number.end()) {
    return it->second;
  }
  auto n = node_number.size();
  node_number.insert({node, n});
  return n;
}

std::string NodeName(Node* node) {
  if (node == nullptr) {
    return "null";
  }
  ostringstream oss;
  oss << "Node_" << NodeNo(node);
  return oss.str();
}

void PrintAST(std::ostream& os, Node* ast, int indent, bool recursive) {
  if (ast == nullptr) {
    os << "null";
    return;
  }

  os << NodeName(ast) << "{" << magic_enum::enum_name(ast->kind)
     << " \"" << (ast->token ? ast->token->raw : "") << "\"";

  if (ast->kind == Node::kInt) {
    os << " value=" << get<opela_type::Int>(ast->value);
  }

  const bool multiline = recursive && (
      ast->lhs || ast->rhs || ast->cond || ast->next);
  if (multiline) {
    os << '\n' << string(indent + 2, ' ') << "lhs=";
    PrintAST(os, ast->lhs, indent + 2, recursive);
    os << '\n' << string(indent + 2, ' ') << "rhs=";
    PrintAST(os, ast->rhs, indent + 2, recursive);
    os << '\n' << string(indent + 2, ' ') << "cond=";
    PrintAST(os, ast->cond, indent + 2, recursive);
    os << '\n' << string(indent + 2, ' ') << "next=";
    PrintAST(os, ast->next, indent + 2, recursive);
  } else {
    os << " lhs=" << NodeName(ast->lhs) << " rhs=" << NodeName(ast->rhs)
       << " cond=" << NodeName(ast->cond) << " next=" << NodeName(ast->next);
  }

  if (multiline) {
    os << '\n' << string(indent, ' ') << '}';
  } else {
    os << '}';
  }
}

} // namespace

Node* Program(Source& src, Tokenizer& t) {
  auto dummy_func_name = new Token{Token::kId, "main", {}};
  auto func = NewFunc(dummy_func_name);

  Scope sc;
  sc.Enter();
  ASTContext ctx{src, t, sc, func->locals};
  Node* node = NewNode(Node::kDefFunc, dummy_func_name);
  node->lhs = Statement(ctx);
  node->value = func;

  ctx.t.Expect(Token::kEOF);
  return node;
}

Node* Statement(ASTContext& ctx) {
  if (ctx.t.Peek("{")) {
    return CompoundStatement(ctx);
  }
  if (auto token = ctx.t.Consume(Token::kRet)) {
    auto node = NewNodeOneChild(Node::kRet, token, ExpressionStatement(ctx));
    return node;
  }
  if (ctx.t.Peek(Token::kIf)) {
    return SelectionStatement(ctx);
  }
  if (ctx.t.Peek(Token::kFor)) {
    return IterationStatement(ctx);
  }

  return ExpressionStatement(ctx);
}

Node* CompoundStatement(ASTContext& ctx) {
  ctx.sc.Enter();

  auto node = NewNode(Node::kBlock, ctx.t.Expect("{"));
  auto cur = node;
  while (!ctx.t.Consume("}")) {
    cur->next = Statement(ctx);
    while (cur->next) {
      cur = cur->next;
    }
  }

  ctx.sc.Leave();
  return node;
}

Node* SelectionStatement(ASTContext& ctx) {
  auto if_token = ctx.t.Expect(Token::kIf);
  auto cond = Expression(ctx);
  auto body_then = CompoundStatement(ctx);
  Node* body_else = nullptr;
  if (ctx.t.Consume(Token::kElse)) {
    if (ctx.t.Peek(Token::kIf)) {
      body_else = SelectionStatement(ctx);
    } else {
      body_else = CompoundStatement(ctx);
    }
  }
  return NewNodeCond(Node::kIf, if_token, cond, body_then, body_else);
}

Node* IterationStatement(ASTContext& ctx) {
  auto for_token = ctx.t.Expect(Token::kFor);
  if (ctx.t.Peek("{")) {
    auto node = NewNode(Node::kLoop, for_token);
    node->lhs = CompoundStatement(ctx);
    return node;
  }

  auto cond = Expression(ctx);
  Node* init = nullptr;
  if (ctx.t.Consume(";")) {
    init = cond;
    cond = Expression(ctx);
    ctx.t.Expect(";");
    init->next = Expression(ctx);
  }
  auto body = CompoundStatement(ctx);
  return NewNodeCond(Node::kFor, for_token, cond, body, init);
}

Node* ExpressionStatement(ASTContext& ctx) {
  auto node = Expression(ctx);
  ctx.t.Expect(";");
  return node;
}

Node* Expression(ASTContext& ctx) {
  return Assignment(ctx);
}

Node* Assignment(ASTContext& ctx) {
  auto node = Equality(ctx);

  if (auto op = ctx.t.Consume("=")) {
    node = NewNodeBinOp(Node::kAssign, op, node, Assignment(ctx));
  } else if (auto op = ctx.t.Consume(":=")) {
    if (node->kind != Node::kId) {
      cerr << "lhs of ':=' must be an identifier" << endl;
      ctx.t.Unexpected(*node->token);
    }

    if (ctx.sc.FindObjectCurrentBlock(node->token->raw)) {
      cerr << "local variable is redefined" << endl;
      ErrorAt(ctx.src, *node->token);
    }

    auto lvar = NewLVar(node->token);
    node->value = lvar;
    ctx.locals.push_back(lvar);
    ctx.sc.PutObject(lvar);

    node = NewNodeBinOp(Node::kDefVar, op, node, Assignment(ctx));
  }

  return node;
}

Node* Equality(ASTContext& ctx) {
  auto node = Relational(ctx);

  for (;;) {
    if (auto op = ctx.t.Consume("==")) {
      node = NewNodeBinOp(Node::kEqu, op, node, Relational(ctx));
    } else if (auto op = ctx.t.Consume("!=")) {
      node = NewNodeBinOp(Node::kNEqu, op, node, Relational(ctx));
    } else {
      return node;
    }
  }
}

Node* Relational(ASTContext& ctx) {
  auto node = Additive(ctx);

  for (;;) {
    if (auto op = ctx.t.Consume("<")) {
      node = NewNodeBinOp(Node::kGT, op, Additive(ctx), node);
    } else if (auto op = ctx.t.Consume("<=")) {
      node = NewNodeBinOp(Node::kLE, op, node, Additive(ctx));
    } else if (auto op = ctx.t.Consume(">")) {
      node = NewNodeBinOp(Node::kGT, op, node, Additive(ctx));
    } else if (auto op = ctx.t.Consume(">=")) {
      node = NewNodeBinOp(Node::kLE, op, Additive(ctx), node);
    } else {
      return node;
    }
  }
}

Node* Additive(ASTContext& ctx) {
  auto node = Multiplicative(ctx);

  for (;;) {
    if (auto op = ctx.t.Consume("+")) {
      node = NewNodeBinOp(Node::kAdd, op, node, Multiplicative(ctx));
    } else if (auto op = ctx.t.Consume("-")) {
      node = NewNodeBinOp(Node::kSub, op, node, Multiplicative(ctx));
    } else {
      return node;
    }
  }
}

Node* Multiplicative(ASTContext& ctx) {
  auto node = Unary(ctx);

  for (;;) {
    if (auto op = ctx.t.Consume("*")) {
      node = NewNodeBinOp(Node::kMul, op, node, Unary(ctx));
    } else if (auto op = ctx.t.Consume("/")) {
      node = NewNodeBinOp(Node::kDiv, op, node, Unary(ctx));
    } else {
      return node;
    }
  }
}

Node* Unary(ASTContext& ctx) {
  if (ctx.t.Consume("+")) {
    return Unary(ctx);
  } else if (auto op = ctx.t.Consume("-")) {
    auto zero = NewNodeInt(nullptr, 0);
    auto node = Unary(ctx);
    return NewNodeBinOp(Node::kSub, op, zero, node);
  }

  return Postfix(ctx);
}

Node* Postfix(ASTContext& ctx) {
  auto node = Primary(ctx);

  for (;;) {
    if (auto op = ctx.t.Consume("(")) {
      node = NewNodeOneChild(Node::kCall, op, node);
      if (!ctx.t.Consume(")")) {
        node->rhs = Expression(ctx);
        for (auto cur = node->rhs; ctx.t.Consume(","); cur = cur->next) {
          cur->next = Expression(ctx);
        }
        ctx.t.Expect(")");
      }
    } else {
      return node;
    }
  }
}

Node* Primary(ASTContext& ctx) {
  if (ctx.t.Consume("(")) {
    auto node = Expression(ctx);
    ctx.t.Expect(")");
    return node;
  } else if (auto id = ctx.t.Consume(Token::kId)) {
    auto node = NewNode(Node::kId, id);
    if (auto obj = ctx.sc.FindObject(id->raw)) {
      node->value = obj;
    }
    return node;
  }
  auto token = ctx.t.Expect(Token::kInt);
  return NewNodeInt(token, get<opela_type::Int>(token->value));
}

void PrintAST(std::ostream& os, Node* ast) {
  PrintAST(os, ast, 0, false);
}

void PrintASTRec(std::ostream& os, Node* ast) {
  PrintAST(os, ast, 0, true);
}
