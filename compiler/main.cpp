#include <iostream>
#include <sstream>

#include "ast.hpp"
#include "source.hpp"
#include "magic_enum.hpp"
#include "token.hpp"

using namespace std;

namespace {

size_t label_counter{0};
string GenerateLabel() {
  ostringstream oss;
  oss << "LABEL" << label_counter++;
  return oss.str();
}

} // namespace

void GenerateCmpSet(ostream& os, const char* cc) {
  os << "    cmp rax, rdi\n";
  os << "    set" << cc << " al\n";
  os << "    movzx eax, al\n";
}

// ローカル変数のアドレスを rax に書く
void LoadLVarAddr(ostream& os, Node* node) {
  if (node->kind != Node::kLVar) {
    cerr << "cannot load address for "
         << magic_enum::enum_name(node->kind) << endl;
    ErrorAt(node->token->loc);
  } else if (node->value.lvar->offset == 0) {
    cerr << "undefined variable '"
         << node->token->Raw() << "'" << endl;
    ErrorAt(node->token->loc);
  }

  os << "    lea rax, [rbp - " << node->value.lvar->offset << "]\n";
}

void GenerateAsm(ostream& os, Node* node, bool lval = false) {
  switch (node->kind) {
  case Node::kInt:
    os << "    push qword " << node->value.i << '\n';
    return;
  case Node::kLVar:
    LoadLVarAddr(os, node);
    os << "    push " << (lval ? "rax" : "qword [rax]") << "\n";
    return;
  case Node::kRet:
    GenerateAsm(os, node->lhs);
    os << "    pop rax\n";
    os << "    jmp main_exit\n";
    return;
  case Node::kIf:
    {
      auto label_else{GenerateLabel()};
      auto label_exit{GenerateLabel()};
      os << "    push rax\n";
      GenerateAsm(os, node->cond);
      os << "    pop rax\n";
      os << "    test rax, rax\n";
      os << "    jz " << label_else << "\n";
      os << "    pop rax\n";
      GenerateAsm(os, node->lhs);
      os << "    jmp " << label_exit << "\n";
      os << label_else << ":\n";
      if (node->rhs) {
        GenerateAsm(os, node->rhs);
      }
      os << label_exit << ":\n";
    }
    return;
  case Node::kLoop:
    {
      auto label_loop{GenerateLabel()};
      os << label_loop << ":\n";
      GenerateAsm(os, node->lhs);
      os << "    pop rax\n";
      os << "    jmp " << label_loop << "\n";
    }
    return;
  case Node::kFor:
    // for init; cond; succ { ... }
    // init: node->rhs
    // cond: node->cond
    // succ: node->rhs->next
    {
      auto label_loop{GenerateLabel()};
      auto label_cond{GenerateLabel()};
      if (node->rhs) {
        GenerateAsm(os, node->rhs);
        os << "    pop rax\n";
      }
      os << "    push qword [rsp]\n";
      os << "    jmp " << label_cond << "\n";
      os << label_loop << ":\n";
      os << "    pop rax\n";
      GenerateAsm(os, node->lhs);
      if (node->rhs) {
        GenerateAsm(os, node->rhs->next);
        os << "    pop rax\n";
      }
      os << label_cond << ":\n";
      GenerateAsm(os, node->cond);
      os << "    pop rax\n";
      os << "    test rax, rax\n";
      os << "    jnz " << label_loop << "\n";
    }
    return;
  case Node::kBlock:
    if (node->next == nullptr) {
      os << "    push rax\n";
      return;
    }
    for (node = node->next; node != nullptr; node = node->next) {
      GenerateAsm(os, node);
      if (node->next) {
        os << "    pop rax\n";
      }
    }
    return;
  case Node::kCall:
    {
      auto f{node->lhs};
      if (f->kind == Node::kLVar && f->value.lvar->offset == 0) {
        // 外部の関数だと仮定する
        auto fname{f->token->Raw()};
        os << "extern " << fname << "\n";
        os << "    mov rax, " << fname << "\n";
      } else if (f->kind == Node::kLVar) {
        LoadLVarAddr(os, f);
      } else {
        GenerateAsm(os, f, true);
        os << "    pop rax\n";
      }
      os << "    call rax\n";
      os << "    push rax\n";
    }
    return;
  default: // caseが足りないという警告を抑制する
    break;
  }

  GenerateAsm(os, node->lhs, node->kind == Node::kAssign);
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
  case Node::kAssign:
    os << "    mov [rax], rdi\n";
    os << "    push " << (lval ? "rax" : "rdi") << "\n";
    return;
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
  cout << "main_exit:\n";
  cout << "    mov rsp, rbp\n";
  cout << "    pop rbp\n";
  cout << "    ret\n";
}
