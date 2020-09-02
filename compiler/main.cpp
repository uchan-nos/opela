#include <iostream>

#include "ast.hpp"
#include "source.hpp"
#include "magic_enum.hpp"
#include "token.hpp"

using namespace std;

void GenerateCmpSet(const char* cc) {
  cout << "    cmp rax, rdi\n";
  cout << "    set" << cc << " al\n";
  cout << "    movzx eax, al\n";
}

void GenerateAsm(Node* node) {
  if (node->kind == Node::kInt) {
    cout << "    push " << node->value << '\n';
    return;
  }

  GenerateAsm(node->lhs);
  GenerateAsm(node->rhs);

  cout << "    pop rdi\n";
  cout << "    pop rax\n";

  switch (node->kind) {
  case Node::kAdd:
    cout << "    add rax, rdi\n";
    break;
  case Node::kSub:
    cout << "    sub rax, rdi\n";
    break;
  case Node::kMul:
    cout << "    imul rax, rdi\n";
    break;
  case Node::kDiv:
    cout << "    cqo\n";
    cout << "    idiv rdi\n";
    break;
  case Node::kInt: // 関数先頭のif文ではじかれている
    break;
  case Node::kEqu:
    GenerateCmpSet("e");
    break;
  case Node::kNEqu:
    GenerateCmpSet("ne");
    break;
  case Node::kLT:
    GenerateCmpSet("l");
    break;
  case Node::kLE:
    GenerateCmpSet("le");
    break;
  }

  cout << "    push rax\n";
}

int main() {
  ReadAll(cin);
  auto tokens{Tokenize(&src[0])};
  cur_token = tokens.begin();

  cout << "bits 64\nsection .text\n";
  cout << "global main\n";
  cout << "main:\n";

  for (auto node{Program()}; node != nullptr; node = node->next) {
    GenerateAsm(node);
    cout << "    pop rax\n";
  }

  cout << "    ret\n";
}
