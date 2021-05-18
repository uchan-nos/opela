#include "ast.hpp"

#include <map>
#include <sstream>
#include <string>

#include "magic_enum.hpp"

using namespace std;

namespace {

Node* NewNodeInt(Token* token, opela_type::Int value) {
  return new Node{Node::kInt, token, nullptr, nullptr, value};
}

Node* NewNodeBinOp(Node::Kind kind, Token* op, Node* lhs, Node* rhs) {
  return new Node{kind, op, lhs, rhs, {}};
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

  const bool multiline = recursive && (ast->lhs || ast->rhs);
  if (multiline) {
    os << '\n' << string(indent + 2, ' ') << "lhs=";
    PrintAST(os, ast->lhs, indent + 2, recursive);
    os << '\n' << string(indent + 2, ' ') << "rhs=";
    PrintAST(os, ast->rhs, indent + 2, recursive);
  } else {
    os << " lhs=" << NodeName(ast->lhs) << " rhs=" << NodeName(ast->rhs);
  }

  if (multiline) {
    os << '\n' << string(indent, ' ') << '}';
  } else {
    os << '}';
  }
}

} // namespace

Node* Expression(Tokenizer& t) {
  return Equality(t);
}

Node* Equality(Tokenizer& t) {
  auto node = Relational(t);

  for (;;) {
    if (auto op = t.Consume("==")) {
      node = NewNodeBinOp(Node::kEqu, op, node, Relational(t));
    } else if (auto op = t.Consume("!=")) {
      node = NewNodeBinOp(Node::kNEqu, op, node, Relational(t));
    } else {
      return node;
    }
  }
}

Node* Relational(Tokenizer& t) {
  auto node = Additive(t);

  for (;;) {
    if (auto op = t.Consume("<")) {
      node = NewNodeBinOp(Node::kGT, op, Additive(t), node);
    } else if (auto op = t.Consume("<=")) {
      node = NewNodeBinOp(Node::kLE, op, node, Additive(t));
    } else if (auto op = t.Consume(">")) {
      node = NewNodeBinOp(Node::kGT, op, node, Additive(t));
    } else if (auto op = t.Consume(">=")) {
      node = NewNodeBinOp(Node::kLE, op, Additive(t), node);
    } else {
      return node;
    }
  }
}

Node* Additive(Tokenizer& t) {
  auto node = Multiplicative(t);

  for (;;) {
    if (auto op = t.Consume("+")) {
      node = NewNodeBinOp(Node::kAdd, op, node, Multiplicative(t));
    } else if (auto op = t.Consume("-")) {
      node = NewNodeBinOp(Node::kSub, op, node, Multiplicative(t));
    } else {
      return node;
    }
  }
}

Node* Multiplicative(Tokenizer& t) {
  auto node = Unary(t);

  for (;;) {
    if (auto op = t.Consume("*")) {
      node = NewNodeBinOp(Node::kMul, op, node, Primary(t));
    } else if (auto op = t.Consume("/")) {
      node = NewNodeBinOp(Node::kDiv, op, node, Primary(t));
    } else {
      return node;
    }
  }
}

Node* Unary(Tokenizer& t) {
  if (t.Consume("+")) {
    return Unary(t);
  } else if (auto op = t.Consume("-")) {
    auto zero = NewNodeInt(nullptr, 0);
    auto node = Unary(t);
    return NewNodeBinOp(Node::kSub, op, zero, node);
  }

  return Primary(t);
}

Node* Primary(Tokenizer& t) {
  if (t.Consume("(")) {
    auto node = Expression(t);
    t.Expect(")");
    return node;
  }
  auto token = t.Expect(Token::kInt);
  return NewNodeInt(token, get<opela_type::Int>(token->value));
}

void PrintAST(std::ostream& os, Node* ast) {
  PrintAST(os, ast, 0, false);
}

void PrintASTRec(std::ostream& os, Node* ast) {
  PrintAST(os, ast, 0, true);
}
