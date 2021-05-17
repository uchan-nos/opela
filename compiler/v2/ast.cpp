#include "ast.hpp"

#include <string>

#include "magic_enum.hpp"

using namespace std;

namespace {

Node* NewNodeBinOp(Node::Kind kind, Token* op, Node* lhs, Node* rhs) {
  return new Node{kind, op, lhs, rhs, {}};
}

void PrintAST(std::ostream& os, Node* ast, int indent) {
  if (ast == nullptr) {
    os << "null\n";
  }
  os << "Node{" << magic_enum::enum_name(ast->kind)
     << " \"" << ast->token->raw << "\" value=";
  if (ast->kind == Node::kInt) {
    os << get<opela_type::Int>(ast->value) << "}\n";
    return;
  } else if (ast->kind <= Node::kDiv) {
    os << '\n' << string(indent + 2, ' ') << "lhs=";
    PrintAST(os, ast->lhs, indent + 2);
    os << string(indent + 2, ' ') << "rhs=";
    PrintAST(os, ast->rhs, indent + 2);
  }
  os << string(indent, ' ') << "}\n";
}

} // namespace

Node* Expression(Tokenizer& t) {
  return Additive(t);
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
  auto node = Primary(t);

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

Node* Primary(Tokenizer& t) {
  if (t.Consume("(")) {
    auto node = Expression(t);
    t.Expect(")");
    return node;
  }
  auto token = t.Expect(Token::kInt);
  return new Node{Node::kInt, token, nullptr, nullptr,
                  get<opela_type::Int>(token->value)};
}

void PrintAST(std::ostream& os, Node* ast) {
  PrintAST(os, ast, 0);
}
