#include <array>
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

const array<const char*, 6> kArgRegs{"rdi", "rsi", "rdx", "rcx", "r8", "r9"};

Context* cur_ctx; // 現在コード生成中の関数を表すコンテキスト

} // namespace

void GenerateCmpSet(ostream& os, const char* cc) {
  os << "    cmp rax, rdi\n";
  os << "    set" << cc << " al\n";
  os << "    movzx eax, al\n";
}

// シンボルのアドレスを rax に書く
void LoadSymAddr(ostream& os, Token* sym_id) {
  auto sym{LookupSymbol(cur_ctx, sym_id->Raw())};

  if (sym == nullptr) {
    cerr << "undeclared symbol '" << sym_id->Raw() << "'" << endl;
    ErrorAt(sym_id->loc);
  }

  if (sym->kind == Symbol::kLVar) {
    os << "    lea rax, [rbp - " << sym->offset << "]\n";
    return;
  }

  if (sym->kind == Symbol::kFunc) {
    os << "    mov rax, " << sym->token->Raw() << "\n";
    return;
  }

  // グローバルなシンボル
  os << "extern " << sym->token->Raw() << "\n";
  os << "    mov rax, " << sym->token->Raw() << "\n";
}

void GenerateAsm(ostream& os, Node* node, bool lval = false) {
  switch (node->kind) {
  case Node::kInt:
    os << "    push qword " << node->value.i << '\n';
    return;
  case Node::kId:
    LoadSymAddr(os, node->token);
    os << "    push " << (lval ? "rax" : "qword [rax]") << "\n";
    return;
  case Node::kRet:
    GenerateAsm(os, node->lhs);
    os << "    pop rax\n";
    os << "    jmp " << cur_ctx->func_name << "_exit\n";
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
      if (node->lhs->type == nullptr) {
        cerr << "node->lhs->type cannot be null" << endl;
        ErrorAt(node->lhs->token->loc);
      }
      switch (node->lhs->type->kind) {
      case Type::kFunc:
        break;
      case Type::kPointer:
        if (node->lhs->type->base->kind != Type::kFunc) {
          cerr << "cannot call non-function pointer" << endl;
          ErrorAt(node->lhs->token->loc);
        }
        break;
      default:
        cerr << "node->lhs = "
             << magic_enum::enum_name(node->lhs->kind)
             << ", " << node->lhs->token->Raw() << endl;
        cerr << "cannot call "
             << magic_enum::enum_name(node->lhs->type->kind) << endl;
        ErrorAt(node->lhs->token->loc);
      }

      int num_arg{0};
      for (auto arg{node->rhs->next}; arg != nullptr; arg = arg->next) {
        if (num_arg == kArgRegs.size()) {
          cerr << "# of arguments must be <= " << kArgRegs.size() << endl;
          ErrorAt(arg->token->loc);
        }
        GenerateAsm(os, arg);
        ++num_arg;
      }

      if (node->lhs->kind == Node::kId) {
        LoadSymAddr(os, node->lhs->token);
        auto func_sym{LookupSymbol(cur_ctx, node->lhs->token->Raw())};
        if (func_sym->type->kind == Type::kPointer) {
          os << "    mov rax, [rax]\n";
        } else if (func_sym->type->kind != Type::kFunc) {
          cerr << "cannot call "
               << magic_enum::enum_name(func_sym->type->kind) << endl;
          ErrorAt(node->lhs->token->loc);
        }
      } else {
        GenerateAsm(os, node->lhs);
        os << "    pop rax\n";
      }

      for (; num_arg > 0; --num_arg) {
        os << "    pop " << kArgRegs[num_arg - 1] << "\n";
      }

      os << "    call rax\n";
      os << "    push rax\n";
    }
    return;
  case Node::kDeclSeq:
    for (auto decl{node->next}; decl != nullptr; decl = decl->next) {
      cur_ctx = contexts[decl->token->Raw()];
      GenerateAsm(os, decl);
    }
    return;
  case Node::kDefFunc:
    os << "global " << cur_ctx->func_name << "\n";
    os << cur_ctx->func_name << ":\n";
    os << "    push rbp\n";
    os << "    mov rbp, rsp\n";
    os << "    sub rsp, " << cur_ctx->StackSize() << "\n";
    os << "    xor rax, rax\n";
    for (size_t i = 0; i < cur_ctx->params.size(); ++i) {
      auto off{cur_ctx->params[i]->offset};
      os << "    mov [rbp - " << off << "], " << kArgRegs[i] << "\n";
    }
    GenerateAsm(os, node->lhs); // 関数ボディ
    os << "    pop rax\n";
    os << cur_ctx->func_name << "_exit:\n";
    os << "    mov rsp, rbp\n";
    os << "    pop rbp\n";
    os << "    ret\n";
    return;
  case Node::kDefVar:
    if (node->rhs) { // 初期値付き変数定義
      break;
    }
    os << "    push rax\n"; // dummy push
    return;
  case Node::kExtern:
    return;
  default: // caseが足りないという警告を抑制する
    break;
  }

  const bool request_lval{
    node->kind == Node::kAssign ||
    node->kind == Node::kAddr ||
    node->kind == Node::kDefVar
  };

  GenerateAsm(os, node->lhs, request_lval);
  if (node->rhs) {
    // 単項演算子の場合は rhs が null の場合がある
    GenerateAsm(os, node->rhs);
    os << "    pop rdi\n";
  }
  os << "    pop rax\n";

  switch (node->kind) {
  case Node::kAdd:
    if (node->type->kind == Type::kInt) {
      os << "    add rax, rdi\n";
    } else if (node->type->kind == Type::kPointer) {
      if (node->lhs->type->kind == Type::kPointer) { // ptr + int
        os << "    lea rax, [rax + 8 * rdi]\n";
      } else {                                       // int + ptr
        os << "    lea rax, [rdi + 8 * rax]\n";
      }
    }
    break;
  case Node::kSub:
    if (node->type->kind == Type::kInt) {
      if (node->lhs->type->kind == Type::kPointer) { // ptr - ptr
        os << "    sub rax, rdi\n";
        os << "    shr rax, 3\n";
      } else {                                       // int - int
        os << "    sub rax, rdi\n";
      }
    } else if (node->type->kind == Type::kPointer) { // ptr - int
      os << "    neg rdi\n";
      os << "    lea rax, [rax + 8 * rdi]\n";
    }
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
  case Node::kDefVar:
    os << "    mov [rax], rdi\n";
    os << "    push " << (lval ? "rax" : "rdi") << "\n";
    return;
  case Node::kAddr:
    // pass
    break;
  case Node::kDeref:
    if (!lval) {
      os << "    mov rax, [rax]\n";
    }
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
  generate_mode = false;
  Program();

  ostringstream oss;
  cur_token = tokens.begin();
  generate_mode = true;
  GenerateAsm(oss, Program());

  cout << "bits 64\nsection .text\n";
  //cout << "global main\n";
  //cout << "main:\n";
  //cout << "    push rbp\n";
  //cout << "    mov rbp, rsp\n";
  //cout << "    sub rsp, " << LVarBytes() << "\n";
  cout << oss.str();
  //cout << "main_exit:\n";
  //cout << "    mov rsp, rbp\n";
  //cout << "    pop rbp\n";
  //cout << "    ret\n";
}
