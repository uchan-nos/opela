#pragma once

#include <array>
#include <ostream>
#include <string_view>

#include "ast.hpp"

class Asm {
 public:
  enum Register {
    kRegL,
    kRegR,
    kRegNum,
  };

  virtual void Push64(std::ostream& os, uint64_t v) = 0;
  virtual void Push64(std::ostream& os, Register reg) = 0;
  virtual void Pop64(std::ostream& os, Register reg) = 0;
  virtual void Add64(std::ostream& os, Register lhs, Register rhs) = 0;
  virtual void Sub64(std::ostream& os, Register lhs, Register rhs) = 0;

  virtual void FuncPrologue(std::ostream& os, Context* ctx) = 0;
  virtual void FuncEpilogue(std::ostream& os, Context* ctx) = 0;
  virtual void SectionText(std::ostream& os) = 0;
};

class AsmX8664 : public Asm {
 public:
  static constexpr std::array<const char*, kRegNum> kRegNames{
    "rax", "rdi"
  };

  void Push64(std::ostream& os, uint64_t v) override {
    os << "    push qword " << v << '\n';
  }

  void Push64(std::ostream& os, Register reg) override {
    os << "    push " << kRegNames[reg] << '\n';
  }

  void Pop64(std::ostream& os, Register reg) override {
    os << "    pop " << kRegNames[reg] << "\n";
  }

  void Add64(std::ostream& os, Register lhs, Register rhs) override {
    os << "    add " << kRegNames[lhs] << ", " << kRegNames[rhs] << "\n";
  }

  void Sub64(std::ostream& os, Register lhs, Register rhs) override {
    os << "    sub " << kRegNames[lhs] << ", " << kRegNames[rhs] << "\n";
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

class AsmAArch64 : public Asm {
 public:
  static constexpr std::array<const char*, kRegNum> kRegNames{
    "x0", "x1"
  };

  void Push64(std::ostream& os, uint64_t v) override {
    os << "    mov x0, " << v << '\n';
    os << "    str x0, [sp, #-16]!\n";
  }

  void Push64(std::ostream& os, Register reg) override {
    os << "    str " << kRegNames[reg] << ", [sp, #-16]!\n";
  }

  void Pop64(std::ostream& os, Register reg) override {
    os << "    ldr " << kRegNames[reg] << ", [sp], #16\n";
  }

  void Add64(std::ostream& os, Register lhs, Register rhs) override {
    os << "    add " << kRegNames[lhs] << ", "
       << kRegNames[lhs] << ", " << kRegNames[rhs] << "\n";
  }

  void Sub64(std::ostream& os, Register lhs, Register rhs) override {
    os << "    sub " << kRegNames[lhs] << ", "
       << kRegNames[lhs] << ", " << kRegNames[rhs] << "\n";
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
