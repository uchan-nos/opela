#pragma once

#include <ostream>
#include <string_view>

#include "ast.hpp"

class AsmGenerator {
 public:
  virtual void PushLiteral64(std::ostream& os, uint64_t v) = 0;
  virtual void FuncPrologue(std::ostream& os, Context* ctx) = 0;
  virtual void FuncEpilogue(std::ostream& os, Context* ctx) = 0;
  virtual void SectionText(std::ostream& os) = 0;
};

class AsmGeneratorX8664 : public AsmGenerator {
  void PushLiteral64(std::ostream& os, uint64_t v) override {
    os << "    push qword " << v << '\n';
  }

  void FuncPrologue(std::ostream& os, Context* ctx) override {
    os << "global " << ctx->func_name << "\n";
    os << ctx->func_name << ":\n";

    os << "    push rbp\n";
    os << "    mov rbp, rsp\n";
    os << "    sub rsp, " << ((ctx->StackSize() + 15) & 0xfffffff0) << "\n";
    os << "    xor rax, rax\n";
  }

  void FuncEpilogue(std::ostream& os, Context* ctx) override {
    os << "    pop rax\n";
    os << ctx->func_name << "_exit:\n";
    os << "    mov rsp, rbp\n";
    os << "    pop rbp\n";
    os << "    ret\n";
  }

  void SectionText(std::ostream& os) override {
    os << "bits 64\nsection .text\n";
  }
};

class AsmGeneratorAArch64 : public AsmGenerator {
  void PushLiteral64(std::ostream& os, uint64_t v) override {
    os << "    mov x0, " << v << '\n';
    os << "    str x0, [sp, #-16]!\n";
  }

  void FuncPrologue(std::ostream& os, Context* ctx) override {
    os << ".global _" << ctx->func_name << "\n";
    os << ".p2align 2\n";
    os << '_' << ctx->func_name << ":\n";
  }

  void FuncEpilogue(std::ostream& os, Context* ctx) override {
    os << "    ldr x0, [sp], #16\n";
    os << "    ret\n";
  }

  void SectionText(std::ostream& os) override {
  }
};
