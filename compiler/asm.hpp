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
    kRegBP,
    kRegSP,
    kRegNum,
  };

  enum Compare {
    kCmpE,
    kCmpNE,
    kCmpL,
    kCmpLE,
  };

  virtual void Push64(std::ostream& os, uint64_t v) = 0;
  virtual void Push64(std::ostream& os, Register reg) = 0;
  virtual void Pop64(std::ostream& os, Register reg) = 0;
  virtual void Add64(std::ostream& os, Register lhs, Register rhs) = 0;
  virtual void Sub64(std::ostream& os, Register lhs, Register rhs) = 0;
  virtual void IMul64(std::ostream& os, Register lhs, Register rhs) = 0;
  virtual void IDiv64(std::ostream& os, Register lhs, Register rhs) = 0;
  virtual void LEA(std::ostream& os, Register dest, Register base, int disp) = 0;
  virtual void Store64(std::ostream& os, Register addr, Register value) = 0;
  // LoadPushN loads N-bit value from addr, push it on stack as 64-bit value
  virtual void LoadPush64(std::ostream& os, Register addr) = 0;
  virtual void LoadSymAddr(std::ostream& os, Register dest,
                           std::string_view label) = 0;

  virtual void Jmp(std::ostream& os, std::string_view label) = 0;
  virtual void JmpIfZero(std::ostream& os, Register reg,
                         std::string_view label) = 0;
  virtual void JmpIfNotZero(std::ostream& os, Register reg,
                            std::string_view label) = 0;
  virtual void Call(std::ostream& os, Register addr) = 0;
  virtual void CmpSet(std::ostream& os, Compare c, Register lhs, Register rhs) = 0;

  virtual void FuncPrologue(std::ostream& os, Context* ctx) = 0;
  virtual void FuncEpilogue(std::ostream& os, Context* ctx) = 0;
  virtual void SectionText(std::ostream& os) = 0;
  virtual void ExternSym(std::ostream& os, std::string_view label) = 0;
};

class AsmX8664 : public Asm {
 public:
  static constexpr std::array<const char*, kRegNum> kRegNames{
    "rax", "rdi", "rbp", "rsp",
  };
  static constexpr std::array<const char*, kRegNum> kRegNames32{
    "eax", "edi", "ebp", "esp",
  };

  void Push64(std::ostream& os, uint64_t v) override {
    os << "    push " << v << '\n';
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

  void IMul64(std::ostream& os, Register lhs, Register rhs) override {
    os << "    imul " << kRegNames[lhs] << ", " << kRegNames[rhs] << "\n";
  }

  void IDiv64(std::ostream& os, Register lhs, Register rhs) override {
    if (lhs == kRegL) {
      os << "    cqo\n";
      os << "    idiv " << kRegNames[rhs] << "\n";
    } else {
      std::cerr << "div supports only rax" << std::endl;
    }
  }

  void LEA(std::ostream& os, Register dest, Register base, int disp) override {
    os << "    lea " << kRegNames[dest]
       << ", [" << kRegNames[base] << " + " << disp << "]\n";
  }

  void Store64(std::ostream& os, Register addr, Register value) override {
    os << "    mov [" << kRegNames[addr] << "], " << kRegNames[value] << "\n";
  }

  void LoadPush64(std::ostream& os, Register addr) override {
    os << "    push qword ptr [" << kRegNames[addr] << "]\n";
  }

  void LoadSymAddr(std::ostream& os, Register dest,
                   std::string_view label) override {
    os << "    movabs " << kRegNames[dest] << ", offset " << label << "\n";
  }

  void Jmp(std::ostream& os, std::string_view label) override {
    os << "    jmp " << label << "\n";
  }

  void JmpIfZero(std::ostream& os, Register reg,
                 std::string_view label) override {
    os << "    test " << kRegNames[reg] << ", " << kRegNames[reg] << "\n";
    os << "    jz " << label << "\n";
  }

  void JmpIfNotZero(std::ostream& os, Register reg,
                    std::string_view label) override {
    os << "    test " << kRegNames[reg] << ", " << kRegNames[reg] << "\n";
    os << "    jnz " << label << "\n";
  }

  void Call(std::ostream& os, Register addr) override {
    os << "    call " << kRegNames[addr] << "\n";
  }

  void CmpSet(std::ostream& os, Compare c, Register lhs, Register rhs) override {
    os << "    cmp " << kRegNames[lhs] << ", " << kRegNames[rhs] << "\n";
    os << "    set";
    switch (c) {
      case kCmpE:  os << "e"; break;
      case kCmpNE: os << "ne"; break;
      case kCmpL:  os << "l"; break;
      case kCmpLE: os << "le"; break;
    }
    os << " al\n";
    os << "    movzx " << kRegNames32[lhs] << ", al\n";
  }

  void FuncPrologue(std::ostream& os, Context* ctx) override {
    os << ".global " << ctx->func_name << "\n";
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
    os << ".code64\n.section .text\n";
  }

  void ExternSym(std::ostream& os, std::string_view label) override {
    os << ".extern " << label << "\n";
  }
};

class AsmAArch64 : public Asm {
 public:
  static constexpr std::array<const char*, kRegNum> kRegNames{
    "x0", "x1", "x29", "sp",
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

  void IMul64(std::ostream& os, Register lhs, Register rhs) override {
    os << "    mul " << kRegNames[lhs] << ", "
       << kRegNames[lhs] << ", " << kRegNames[rhs] << "\n";
  }

  void IDiv64(std::ostream& os, Register lhs, Register rhs) override {
    os << "    sdiv " << kRegNames[lhs] << ", "
       << kRegNames[lhs] << ", " << kRegNames[rhs] << "\n";
  }

  void LEA(std::ostream& os, Register dest, Register base, int disp) override {
    os << "    add " << kRegNames[dest]
       << ", " << kRegNames[base] << ", " << disp << "\n";
  }

  void Store64(std::ostream& os, Register addr, Register value) override {
    os << "    str " << kRegNames[value] << ", [" << kRegNames[addr] << "]\n";
  }

  void LoadPush64(std::ostream& os, Register addr) override {
    os << "    ldr x0, [" << kRegNames[addr] << "]\n";
    os << "    str x0, [sp, #-16]!\n";
  }

  void LoadSymAddr(std::ostream& os, Register dest,
                   std::string_view label) override {
    os << "    adrp " << kRegNames[dest] << ", _" << label << "@PAGE\n";
    os << "    add " << kRegNames[dest] << ", " << kRegNames[dest]
       << ", _" << label << "@PAGEOFF\n";
  }

  void Jmp(std::ostream& os, std::string_view label) override {
    os << "    b " << label << "\n";
  }

  void JmpIfZero(std::ostream& os, Register reg,
                 std::string_view label) override {
    os << "    cbz " << kRegNames[reg] << ", " << label << "\n";
  }

  void JmpIfNotZero(std::ostream& os, Register reg,
                    std::string_view label) override {
    os << "    cbnz " << kRegNames[reg] << ", " << label << "\n";
  }

  void Call(std::ostream& os, Register addr) override {
    os << "    blr " << kRegNames[addr] << "\n";
  }

  void CmpSet(std::ostream& os, Compare c, Register lhs, Register rhs) override {
    os << "    cmp " << kRegNames[lhs] << ", " << kRegNames[rhs] << "\n";
    os << "    cset " << kRegNames[lhs] << ", ";
    switch (c) {
      case kCmpE:  os << "eq"; break;
      case kCmpNE: os << "ne"; break;
      case kCmpL:  os << "lt"; break;
      case kCmpLE: os << "le"; break;
    }
    os << "\n";
  }

  void FuncPrologue(std::ostream& os, Context* ctx) override {
    os << ".global _" << ctx->func_name << "\n";
    os << ".p2align 2\n";
    os << '_' << ctx->func_name << ":\n";
    os << "    stp x29, x30, [sp, #-16]!\n";
    os << "    mov x29, sp\n";
    os << "    sub sp, sp, " << ((ctx->StackSize() + 15) & 0xfffffff0) << "\n";
    os << "    mov x0, xzr\n";
  }

  void FuncEpilogue(std::ostream& os, Context* ctx) override {
    os << "    ldr x0, [sp], #16\n";
    os << ctx->func_name << "_exit:\n";
    os << "    mov sp, x29\n";
    os << "    ldp x29, x30, [sp], #16\n";
    os << "    ret\n";
  }

  void SectionText(std::ostream& os) override {
  }

  void ExternSym(std::ostream& os, std::string_view label) override {
    os << ".extern _" << label << "\n";
  }
};
