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
    kRegArg0,
    kRegArg1,
    kRegArg2,
    kRegArg3,
    kRegArg4,
    kRegArg5,
    kRegRet,
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
  virtual void LEA(std::ostream& os, Register dest, Register base,
               int scale, Register index) = 0;
  virtual void Load64(std::ostream& os, Register dest,
                      Register addr, int disp) = 0;
  virtual void Store64(std::ostream& os, Register addr, int disp,
                       Register value) = 0;
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
    "rdi", "rsi", "rdx", "rcx", "r8", "r9", "rax",
  };
  static constexpr std::array<const char*, kRegNum> kRegNames32{
    "eax", "edi", "ebp", "esp",
    "edi", "esi", "edx", "ecx", "r8d", "r9d", "eax",
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

  void LEA(std::ostream& os, Register dest, Register base,
           int scale, Register index) override {
    os << "    lea " << kRegNames[dest] << ", [" << kRegNames[base]
       << " + " << scale << "*" << kRegNames[index] << "]\n";
  }

  void Load64(std::ostream& os, Register dest,
              Register addr, int disp) override {
    os << "    mov " << kRegNames[dest] << ", ["
       << kRegNames[addr] << "+" << disp << "]\n";
  }

  void Store64(std::ostream& os, Register addr, int disp,
               Register value) override {
    os << "    mov [" << kRegNames[addr] << "+" << disp << "], "
       << kRegNames[value] << "\n";
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
    os << ".intel_syntax noprefix\n";
    os << ".code64\n.section .text\n";
  }

  void ExternSym(std::ostream& os, std::string_view label) override {
    os << ".extern " << label << "\n";
  }
};

class AsmAArch64 : public Asm {
 public:
  static constexpr std::array<const char*, kRegNum> kRegNames{
    "x8", "x9", "x29", "sp",
    "x0", "x1", "x2", "x3", "x4", "x5", "x0",
  };

  void Push64(std::ostream& os, uint64_t v) override {
    os << "    mov x8, " << v << '\n';
    os << "    str x8, [sp, #-16]!\n";
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

  void LEA(std::ostream& os, Register dest, Register base,
           int scale, Register index) override {
    const char* op = "add";
    if (scale < 0) {
      op = "sub";
      scale = -scale;
    }

    int shift_amount = 0;
    switch (scale) {
    case 1: shift_amount = 0; break;
    case 2: shift_amount = 1; break;
    case 4: shift_amount = 2; break;
    case 8: shift_amount = 3; break;
    default:
      std::cerr << "cannot handle non 2-power scale" << std::endl;
    }
    os << "    " << op << " " << kRegNames[dest] << ", " << kRegNames[base]
       << ", " << kRegNames[index] << ", lsl #" << shift_amount << "\n";
  }

  void Load64(std::ostream& os, Register dest,
              Register addr, int disp) override {
    os << "    ldr " << kRegNames[dest] << ", ["
       << kRegNames[addr] << ", #" << disp << "]\n";
  }

  void Store64(std::ostream& os, Register addr, int disp,
               Register value) override {
    os << "    str " << kRegNames[value] << ", ["
       << kRegNames[addr] << ", #" << disp << "]\n";
  }

  void LoadPush64(std::ostream& os, Register addr) override {
    os << "    ldr x8, [" << kRegNames[addr] << "]\n";
    os << "    str x8, [sp, #-16]!\n";
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
    os << "    mov " << kRegNames[Asm::kRegL] << ", xzr\n";
  }

  void FuncEpilogue(std::ostream& os, Context* ctx) override {
    os << "    ldr " << kRegNames[Asm::kRegRet] << ", [sp], #16\n";
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
