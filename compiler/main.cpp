#include <iostream>
#include <sstream>

#include "ast.hpp"
#include "source.hpp"
#include "magic_enum.hpp"
#include "token.hpp"

using namespace std;

void GenerateCmpSet(ostream& os, const char* cc) {
  os << "    cmp rax, rdi\n";
  os << "    set" << cc << " al\n";
  os << "    movzx eax, al\n";
}

// 左辺値（lvalue）を生成する
void GenerateLVal(ostream& os, Node* node) {
  if (node->kind != Node::kLVar) {
    cerr << "cannot generate lvalue for "
         << magic_enum::enum_name(node->kind) << endl;
    ErrorAt(node->token->loc);
  } else if (node->value.lvar->offset == 0) {
    cerr << "undefined variable '"
         << node->token->Raw() << "'" << endl;
    ErrorAt(node->token->loc);
  }

  os << "    lea rax, [rbp - " << node->value.lvar->offset << "]\n";
  os << "    push rax\n";
}

void GenerateAsm(ostream& os, Node* node) {
  switch (node->kind) {
  case Node::kInt:
    os << "    push qword " << node->value.i << '\n';
    return;
  case Node::kLVar:
    GenerateLVal(os, node);
    os << "    pop rax\n";
    os << "    push qword [rax]\n";
    return;
  case Node::kDefLVar:
    GenerateLVal(os, node->lhs);
    GenerateAsm(os, node->rhs);
    os << "    pop rdi\n";
    os << "    pop rax\n";
    os << "    mov [rax], rdi\n";
    os << "    push rax\n";
    return;
  default: // caseが足りないという警告を抑制する
    break;
  }

  GenerateAsm(os, node->lhs);
  GenerateAsm(os, node->rhs);

  os << "    pop rdi\n";
  os << "    pop rax\n";

  switch (node->kind) {
  case Node::kAdd:
    os << "    add rax, rdi\n";
    break;
  case Node::kSub:
    os << "    sub rax, rdi\n";
    break;
  case Node::kMul:
    os << "    imul rax, rdi\n";
    break;
  case Node::kDiv:
    os << "    cqo\n";
    os << "    idiv rdi\n";
    break;
  case Node::kEqu:
    GenerateCmpSet(os, "e");
    break;
  case Node::kNEqu:
    GenerateCmpSet(os, "ne");
    break;
  case Node::kLT:
    GenerateCmpSet(os, "l");
    break;
  case Node::kLE:
    GenerateCmpSet(os, "le");
    break;
  case Node::kDefLVar:
    os << "    mov [rax], rdi\n";
    break;
  default: // caseが足りないという警告を抑制する
    break;
  }

  os << "    push rax\n";
}

int main() {
  ReadAll(cin);
  auto tokens{Tokenize(&src[0])};
  cur_token = tokens.begin();

  ostringstream oss;
  for (auto node{Program()}; node != nullptr; node = node->next) {
    GenerateAsm(oss, node);
    oss << "    pop rax\n";
  }

  cout << "bits 64\nsection .text\n";
  cout << "global main\n";
  cout << "main:\n";
  cout << "    push rbp\n";
  cout << "    mov rbp, rsp\n";
  cout << "    sub rsp, " << LVarBytes() << "\n";
  cout << oss.str();
  cout << "    mov rsp, rbp\n";
  cout << "    pop rbp\n";
  cout << "    ret\n";
}
