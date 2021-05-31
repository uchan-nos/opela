#include "ast.hpp"

#include <map>
#include <iostream>
#include <sstream>
#include <string>

#include "magic_enum.hpp"
#include "object.hpp"

using namespace std;

namespace {

Node* NewNodeInt(Token* token, opela_type::Int value) {
  return new Node{Node::kInt, token, nullptr, nullptr, nullptr, value};
}

Node* NewNodeBinOp(Node::Kind kind, Token* op, Node* lhs, Node* rhs) {
  return new Node{kind, op, lhs, rhs, nullptr, {}};
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

  const bool multiline = recursive && (ast->next || ast->lhs || ast->rhs);
  if (multiline) {
    os << '\n' << string(indent + 2, ' ') << "lhs=";
    PrintAST(os, ast->lhs, indent + 2, recursive);
    os << '\n' << string(indent + 2, ' ') << "rhs=";
    PrintAST(os, ast->rhs, indent + 2, recursive);
    os << '\n' << string(indent + 2, ' ') << "next=";
    PrintAST(os, ast->next, indent + 2, recursive);
  } else {
    os << " lhs=" << NodeName(ast->lhs) << " rhs=" << NodeName(ast->rhs)
       << " next=" << NodeName(ast->next);
  }

  if (multiline) {
    os << '\n' << string(indent, ' ') << '}';
  } else {
    os << '}';
  }
}

} // namespace

Node* Program(Source& src, Tokenizer& t) {
  Scope sc;
  sc.Enter();
  vector<Object*> locals;
  ASTContext ctx{src, t, sc, locals};

  Node* node = new Node{Node::kDefFunc, nullptr, Statement(ctx)};
  ctx.t.Expect(Token::kEOF);

  node->value = new Object{Object::kFunc, nullptr, false, 0, std::move(locals)};
  return node;
}

Node* Statement(ASTContext& ctx) {
  if (ctx.t.Peek("{")) {
    return CompoundStatement(ctx);
  }
  if (auto token = ctx.t.Consume(Token::kRet)) {
    auto node = new Node{Node::kRet, token, ExpressionStatement(ctx)};
    return node;
  }

  return ExpressionStatement(ctx);
}

Node* CompoundStatement(ASTContext& ctx) {
  ctx.sc.Enter();

  auto node = new Node{Node::kBlock, ctx.t.Expect("{")};
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

  if (auto op = ctx.t.Consume(":=")) {
    if (node->kind != Node::kId) {
      cerr << "lhs of ':=' must be an identifier" << endl;
      ctx.t.Unexpected(*node->token);
    }

    if (auto obj = ctx.sc.FindObjectCurrentBlock(node->token->raw)) {
      cerr << "local variable is redefined" << endl;
      ErrorAt(ctx.src, *node->token);
    }

    auto lvar = new Object{Object::kVar, node->token, true};
    ctx.locals.push_back(lvar);
    ctx.sc.PutObject(lvar);

    auto init = Assignment(ctx);
    node = NewNodeBinOp(Node::kDefVar, op, node, init);
    node->value = lvar;
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

  return Primary(ctx);
}

Node* Primary(ASTContext& ctx) {
  if (ctx.t.Consume("(")) {
    auto node = Expression(ctx);
    ctx.t.Expect(")");
    return node;
  } else if (auto id = ctx.t.Consume(Token::kId)) {
    auto symbol = ctx.sc.FindObject(id->raw);
    auto node = new Node{Node::kId, id};
    node->value = symbol;
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
