#pragma once

#include <array>
#include <limits>
#include <ostream>
#include <string_view>

#include "ast.hpp"

constexpr unsigned WordSize(unsigned bits) {
  if (bits == 0) {
    return 0;
  } else if (bits <= 8) {
    return 1;
  } else if (bits <= 16) {
    return 2;
  } else if (bits <= 32) {
    return 4;
  } else if (bits <= 64) {
    return 8;
  }
  return std::numeric_limits<unsigned>::max();
}

int ShiftAmount(uint64_t value) {
  uint64_t mask = 1;
  for (int i = 0; i < 64; ++i) {
    if (value == mask) {
      return i;
    }
    mask <<= 1;
  }
  return -1; // value is not 2-power number.
}

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
    kRegZero,
    kRegTmp,
    kRegNum,
  };

  enum Compare {
    kCmpE,
    kCmpNE,
    kCmpG,  // a > b (signed)
    kCmpLE, // a <= b (signed)
    kCmpA,  // a > b (unsigned)
    kCmpBE, // a <= b (unsigned)
  };

  virtual void Mov64(std::ostream& os, Register dest, uint64_t value) = 0;
  virtual void Mov64(std::ostream& os, Register dest, Register src) = 0;
  virtual void Push64(std::ostream& os, uint64_t v) = 0;
  virtual void Push64(std::ostream& os, Register reg) = 0;
  virtual void Pop64(std::ostream& os, Register reg) = 0;
  virtual void Add64(std::ostream& os, Register lhs, uint64_t v) = 0;
  virtual void Add64(std::ostream& os, Register lhs, Register rhs) = 0;
  virtual void Sub64(std::ostream& os, Register lhs, uint64_t v) = 0;
  virtual void Sub64(std::ostream& os, Register lhs, Register rhs) = 0;
  virtual void IMul64(std::ostream& os, Register lhs, Register rhs) = 0;
  virtual void IDiv64(std::ostream& os, Register lhs, Register rhs) = 0;
  virtual void ShiftR(std::ostream& os, Register reg, int shift_amount) = 0;
  virtual void LEA(std::ostream& os, Register dest, Register base, int disp) = 0;
  virtual void LEA(std::ostream& os, Register dest, Register base,
               int scale, Register index) = 0;
  virtual void Load64(std::ostream& os, Register dest, Register addr) = 0;
  // LoadN loads scale bytes from [base + scale * index]
  virtual void LoadN(std::ostream& os, Register dest, Register base,
                     int disp, unsigned bits) = 0;
  virtual void LoadN(std::ostream& os, Register dest, Register base,
                     int scale, Register index) = 0;
  virtual void StoreN(std::ostream& os, Register addr, int disp,
                      Register value, unsigned bits) = 0;
  virtual void StoreN(std::ostream& os, std::string_view sym_name,
                      Register value, unsigned bits) = 0;
  // LoadPushN loads N-bit value from addr, push it on stack as 64-bit value
  virtual void LoadPushN(std::ostream& os, Register addr, unsigned bits) = 0;
  virtual void LoadSymAddr(std::ostream& os, Register dest,
                           std::string_view sym_name) = 0;
  virtual void Inc64(std::ostream& os, Register lhs) = 0;
  virtual void Dec64(std::ostream& os, Register lhs) = 0;

  virtual void MaskBits(std::ostream& os, Register reg, unsigned bits) = 0;

  virtual void Jmp(std::ostream& os, std::string_view label) = 0;
  virtual void JmpIfZero(std::ostream& os, Register reg,
                         std::string_view label) = 0;
  virtual void JmpIfNotZero(std::ostream& os, Register reg,
                            std::string_view label) = 0;
  virtual void Call(std::ostream& os, Register addr) = 0;
  virtual void CmpSet(std::ostream& os, Compare c, Register dest,
                      Register lhs, Register rhs) = 0;

  virtual void FuncPrologue(std::ostream& os, Context* ctx) = 0;
  virtual void FuncPrologue(std::ostream& os, std::string_view sym_name) = 0;
  virtual void FuncEpilogue(std::ostream& os, Context* ctx) = 0;
  virtual void FuncEpilogue(std::ostream& os) = 0;
  virtual bool FuncVArgOnStack() = 0;
  virtual void PrepareFuncVArg(std::ostream& os, size_t num_normal,
                               size_t num_variadic) = 0;
  virtual void SectionText(std::ostream& os) = 0;
  virtual void SectionInit(std::ostream& os) = 0;
  virtual void SectionData(std::ostream& os, bool readonly) = 0;
  virtual std::string SymLabel(std::string_view sym_name) = 0;
};

class AsmX8664 : public Asm {
 public:
  static constexpr std::array<const char*, kRegNum> kRegNames{
    "a", "di", "bp", "sp",
    "di", "si", "d", "c", "r8", "r9", "a", "zero", "r10",
  };
  static std::string RegName(std::string stem, unsigned bytes) {
    if (stem == "zero") {
      return "0";
    }
    if (stem.length() == 1) { // a, b, c, d
      switch (bytes) {
      case 1: return stem + "l";
      case 2: return stem + "x";
      case 4: return "e" + stem + "x";
      case 8: return "r" + stem + "x";
      default: return "non-standard size";
      }
    }
    if (stem[0] == 'r') { // r8 - r15
      switch (bytes) {
      case 1: return stem + "b";
      case 2: return stem + "w";
      case 4: return stem + "d";
      case 8: return stem;
      default: return "non-standard size";
      }
    }
    if (stem.length() == 2) { // di, si, bp, sp
      switch (bytes) {
      case 1: return stem + "l";
      case 2: return stem;
      case 4: return "e" + stem;
      case 8: return "r" + stem;
      default: return "non-standard size";
      }
    }
    return "failed to get register name for " + stem;
  }
  static std::string RegName(Register reg, unsigned bytes = 8) {
    return RegName(kRegNames[reg], bytes);
  }

  static std::string WordName(unsigned word_size) {
    switch (word_size) {
    case 1: return "byte";
    case 2: return "word";
    case 4: return "dword";
    case 8: return "qword";
    default: return "unknown word size";
    }
  }

  void Mov64(std::ostream& os, Register dest, uint64_t value) override {
    os << "    mov " << RegName(dest) << ", " << value << "\n";
  }

  void Mov64(std::ostream& os, Register dest, Register src) override {
    os << "    mov " << RegName(dest) << ", " << RegName(src) << "\n";
  }

  void Push64(std::ostream& os, uint64_t v) override {
    os << "    push " << v << '\n';
  }

  void Push64(std::ostream& os, Register reg) override {
    os << "    push " << RegName(reg) << '\n';
  }

  void Pop64(std::ostream& os, Register reg) override {
    os << "    pop " << RegName(reg) << "\n";
  }

  void Add64(std::ostream& os, Register lhs, uint64_t v) override {
    os << "    add " << RegName(lhs) << ", " << v << "\n";
  }

  void Add64(std::ostream& os, Register lhs, Register rhs) override {
    os << "    add " << RegName(lhs) << ", " << RegName(rhs) << "\n";
  }

  void Sub64(std::ostream& os, Register lhs, uint64_t v) override {
    os << "    sub " << RegName(lhs) << ", " << v << "\n";
  }

  void Sub64(std::ostream& os, Register lhs, Register rhs) override {
    os << "    sub " << RegName(lhs) << ", " << RegName(rhs) << "\n";
  }

  void IMul64(std::ostream& os, Register lhs, Register rhs) override {
    os << "    imul " << RegName(lhs) << ", " << RegName(rhs) << "\n";
  }

  void IDiv64(std::ostream& os, Register lhs, Register rhs) override {
    if (lhs == kRegL) {
      os << "    cqo\n";
      os << "    idiv " << RegName(rhs) << "\n";
    } else {
      std::cerr << "div supports only rax" << std::endl;
    }
  }

  void ShiftR(std::ostream& os, Register reg, int shift_amount) override {
    os << "    shr " << RegName(reg) << ", " << shift_amount << "\n";
  }

  void LEA(std::ostream& os, Register dest, Register base, int disp) override {
    os << "    lea " << RegName(dest)
       << ", [" << RegName(base)
       << std::showpos << disp << std::noshowpos << "]\n";
  }

  void LEA(std::ostream& os, Register dest, Register base,
           int scale, Register index) override {
    if (-8 <= scale && scale <= 8) {
      if (scale < 0) {
        os << "    neg " << RegName(index) << "\n";
        scale = -scale;
      }
      os << "    lea " << RegName(dest) << ", [" << RegName(base)
         << " + " << scale << "*" << RegName(index) << "]\n";
    } else {
      os << "    imul r10, " << RegName(index) << ", " << scale << "\n";
      os << "    lea " << RegName(dest) << ", [" << RegName(base) << "+r10]\n";
    }
  }

  void Load64(std::ostream& os, Register dest, Register addr) override {
    os << "    mov " << RegName(dest) << ", [" << RegName(addr) << "]\n";
  }

  void LoadN(std::ostream& os, Register dest, Register base,
             int disp, unsigned bits) override {
    auto ws = WordSize(bits);
    auto d = RegName(dest, ws <= 4 ? 4 : 8);
    if (ws == 0) {
      return;
    } else if (ws <= 2) {
      os << "    movzx ";
    } else {
      os << "    mov ";
    }
    os << RegName(dest, ws <= 4 ? 4 : 8) << ", "
       << WordName(ws) << " ptr [" << RegName(base) << "+" << disp << "]\n";
  }

  void LoadN(std::ostream& os, Register dest, Register base,
             int scale, Register index) override {
    auto d = RegName(dest, scale <= 4 ? 4 : 8);
    auto b = RegName(base);
    auto i = RegName(index);
    switch (scale) {
    case 1:
      os << "    movzx " << d << ", byte ptr [" << b << "+" << i << "]\n";
      break;
    case 2:
      os << "    movzx " << d << ", word ptr [" << b << "+2*" << i << "]\n";
      break;
    case 4:
      os << "    mov " << d << ", [" << b << "+4*" << i << "]\n";
      break;
    case 8:
      os << "    mov " << d << ", [" << b << "+8*" << i << "]\n";
      break;
    default:
      std::cerr << "cannot load at non 2-power scaled address" << std::endl;
    }
  }

  void StoreN(std::ostream& os, Register addr, int disp,
              Register value, unsigned bits) override {
    auto ws = WordSize(bits);
    os << "    mov " << WordName(ws) << " ptr [" << RegName(addr)
       << "+" << disp << "], " << RegName(value, ws) << "\n";
  }

  void StoreN(std::ostream& os, std::string_view sym_name,
              Register value, unsigned bits) override {
    auto ws = WordSize(bits);
    os << "    mov " << WordName(ws) << " ptr [rip+" << sym_name << "], "
       << RegName(value, ws) << "\n";
  }

  void LoadPushN(std::ostream& os, Register addr, unsigned bits) override {
    if (bits <= 8) {
      os << "    xor r10d, r10d\n";
      os << "    mov r10b, [" << RegName(addr) << "]\n";
    } else if (bits <= 16) {
      os << "    xor r10d, r10d\n";
      os << "    mov r10w, [" << RegName(addr) << "]\n";
    } else if (bits <= 32) {
      os << "    mov r10d, [" << RegName(addr) << "]\n";
    } else if (bits <= 64) {
      os << "    push qword ptr [" << RegName(addr) << "]\n";
      return;
    } else {
      std::cerr << "cannot load more than 8 bytes" << std::endl;
    }
    os << "    push r10\n";
  }

  void LoadSymAddr(std::ostream& os, Register dest,
                   std::string_view sym_name) override {
    os << "    movabs " << RegName(dest) << ", offset " << sym_name << "\n";
  }

  void Inc64(std::ostream& os, Register lhs) override {
    os << "    inc qword ptr [" << RegName(lhs) << "]\n";
  }

  void Dec64(std::ostream& os, Register lhs) override {
    os << "    dec qword ptr [" << RegName(lhs) << "]\n";
  }

  void MaskBits(std::ostream& os, Register reg, unsigned bits) override {
    if (bits >= 64) {
      return;
    }
    os << "    mov r10b, " << bits << "\n";
    os << "    bzhi " << RegName(reg) << ", " << RegName(reg) << ", r10\n";
  }

  void Jmp(std::ostream& os, std::string_view label) override {
    os << "    jmp " << label << "\n";
  }

  void JmpIfZero(std::ostream& os, Register reg,
                 std::string_view label) override {
    os << "    test " << RegName(reg) << ", " << RegName(reg) << "\n";
    os << "    jz " << label << "\n";
  }

  void JmpIfNotZero(std::ostream& os, Register reg,
                    std::string_view label) override {
    os << "    test " << RegName(reg) << ", " << RegName(reg) << "\n";
    os << "    jnz " << label << "\n";
  }

  void Call(std::ostream& os, Register addr) override {
    os << "    push rbx\n";
    os << "    mov rbx, rsp\n";
    os << "    and rsp, -16\n";
    os << "    call " << RegName(addr) << "\n";
    os << "    mov rsp, rbx\n";
    os << "    pop rbx\n";
  }

  void CmpSet(std::ostream& os, Compare c, Register dest,
              Register lhs, Register rhs) override {
    os << "    cmp " << RegName(lhs) << ", " << RegName(rhs) << "\n";
    os << "    set";
    switch (c) {
      case kCmpE:  os << "e"; break;
      case kCmpNE: os << "ne"; break;
      case kCmpG:  os << "g"; break;
      case kCmpLE: os << "le"; break;
      case kCmpA:  os << "a"; break;
      case kCmpBE: os << "be"; break;
    }
    os << " al\n";
    os << "    movzx " << RegName(dest, 4) << ", al\n";
  }

  void FuncPrologue(std::ostream& os, Context* ctx) override {
    FuncPrologue(os, ctx->func_name);
    os << "    sub rsp, " << ((ctx->StackSize() + 15) & 0xfffffff0) << "\n";
    os << "    xor rax, rax\n";
  }

  void FuncPrologue(std::ostream& os, std::string_view sym_name) override {
    os << ".global " << sym_name << "\n";
    os << sym_name << ":\n";
    os << "    push rbp\n";
    os << "    mov rbp, rsp\n";
  }

  void FuncEpilogue(std::ostream& os, Context* ctx) override {
    os << "    pop rax\n";
    os << ctx->func_name << "_exit:\n";
    FuncEpilogue(os);
  }

  void FuncEpilogue(std::ostream& os) override {
    os << "    mov rsp, rbp\n";
    os << "    pop rbp\n";
    os << "    ret\n";
  }

  bool FuncVArgOnStack() override {
    return false;
  }

  void PrepareFuncVArg(std::ostream&, size_t, size_t) override {
  }

  void SectionText(std::ostream& os) override {
    os << ".intel_syntax noprefix\n";
    os << ".code64\n.section .text\n";
  }

  void SectionInit(std::ostream& os) override {
    os << ".section .init_array\n";
  }

  void SectionData(std::ostream& os, bool readonly) override {
    os << ".section " << (readonly ? ".rodata\n" : ".data\n");
  }

  std::string SymLabel(std::string_view sym_name) override {
    return std::string{sym_name};
  }
};

class AsmAArch64 : public Asm {
 public:
  static constexpr std::array<const char*, kRegNum> kRegNames{
    "8", "9", "29", "sp",
    "0", "1", "2", "3", "4", "5", "0", "zr", "10",
  };
  static std::string RegName(std::string stem, unsigned bytes) {
    if (stem == "sp") {
      return stem;
    }
    if (bytes == 4) {
      return "w" + stem;
    } else if (bytes == 8) {
      return "x" + stem;
    }
    return "failed to get register name for " + stem;
  }
  static std::string RegName(Register reg, unsigned bytes = 8) {
    return RegName(kRegNames[reg], bytes);
  }

  void Mov64(std::ostream& os, Register dest, uint64_t value) override {
    if (value <= 0xffffu) {
      os << "    mov " << RegName(dest) << ", #" << value << "\n";
      return;
    }

    bool first = true;
    for (int shift = 0; shift < 4; ++shift) {
      if (uint16_t v = value & 0xffffu; v != 0) {
        os << (first ? "    movz " : "    movk ") << RegName(dest)
           << ", #" << v << ", lsl #" << (shift * 16) << "\n";
        first = false;
      }
      value >>= 16;
    }
  }

  void Mov64(std::ostream& os, Register dest, Register src) override {
    os << "    mov " << RegName(dest) << ", " << RegName(src) << "\n";
  }

  void Push64(std::ostream& os, uint64_t v) override {
    Mov64(os, Asm::kRegTmp, v);
    os << "    str x10, [sp, #-16]!\n";
  }

  void Push64(std::ostream& os, Register reg) override {
    os << "    str " << RegName(reg) << ", [sp, #-16]!\n";
  }

  void Pop64(std::ostream& os, Register reg) override {
    os << "    ldr " << RegName(reg) << ", [sp], #16\n";
  }

  void Add64(std::ostream& os, Register lhs, uint64_t v) override {
    os << "    add " << RegName(lhs) << ", "
       << RegName(lhs) << ", #" << v << "\n";
  }

  void Add64(std::ostream& os, Register lhs, Register rhs) override {
    os << "    add " << RegName(lhs) << ", "
       << RegName(lhs) << ", " << RegName(rhs) << "\n";
  }

  void Sub64(std::ostream& os, Register lhs, uint64_t v) override {
    os << "    sub " << RegName(lhs) << ", "
       << RegName(lhs) << ", #" << v << "\n";
  }

  void Sub64(std::ostream& os, Register lhs, Register rhs) override {
    os << "    sub " << RegName(lhs) << ", "
       << RegName(lhs) << ", " << RegName(rhs) << "\n";
  }

  void IMul64(std::ostream& os, Register lhs, Register rhs) override {
    os << "    mul " << RegName(lhs) << ", "
       << RegName(lhs) << ", " << RegName(rhs) << "\n";
  }

  void IDiv64(std::ostream& os, Register lhs, Register rhs) override {
    os << "    sdiv " << RegName(lhs) << ", "
       << RegName(lhs) << ", " << RegName(rhs) << "\n";
  }

  void ShiftR(std::ostream& os, Register reg, int shift_amount) override {
    os << "    lsr " << RegName(reg) << ", "
       << RegName(reg) << ", #" << shift_amount << "\n";
  }

  void LEA(std::ostream& os, Register dest, Register base, int disp) override {
    os << "    add " << RegName(dest)
       << ", " << RegName(base) << ", " << disp << "\n";
  }

  void LEA(std::ostream& os, Register dest, Register base,
           int scale, Register index) override {
    const char* op = "add";
    if (scale < 0) {
      op = "sub";
      scale = -scale;
    }

    int shift_amount = ShiftAmount(scale);
    if (shift_amount == -1) {
      std::cerr << "cannot handle non 2-power scale" << std::endl;
    }
    os << "    " << op << " " << RegName(dest) << ", " << RegName(base)
       << ", " << RegName(index) << ", lsl #" << shift_amount << "\n";
  }

  void Load64(std::ostream& os, Register dest, Register addr) override {
    os << "    ldr " << RegName(dest) << ", [" << RegName(addr) << "]\n";
  }

  void LoadN(std::ostream& os, Register dest, Register base,
             int disp, unsigned bits) override {
    auto ws = WordSize(bits);
    auto d = RegName(dest, ws <= 4 ? 4 : 8);
    switch (ws) {
    case 0:
      return;
    case 1:
      os << "    ldrb ";
      break;
    case 2:
      os << "    ldrh ";
      break;
    case 4:
    case 8:
      os << "    ldr ";
      break;
    }
    os << RegName(dest, ws <= 4 ? 4 : 8) << ", ["
       << RegName(base) << ", #" << disp << "]\n";
  }

  void LoadN(std::ostream& os, Register dest, Register base,
             int scale, Register index) override {
    auto d = RegName(dest, scale <= 4 ? 4 : 8);
    auto b = RegName(base);
    auto i = RegName(index);
    switch (scale) {
    case 1:
      os << "    ldrb " << d << ", [" << b << ", " << i << "]\n";
      break;
    case 2:
      os << "    ldrh " << d << ", [" << b << ", " << i << ", lsl #1]\n";
      break;
    case 4:
      os << "    ldr " << d << ", [" << b << ", " << i << ", lsl #2]\n";
      break;
    case 8:
      os << "    ldr " << d << ", [" << b << ", " << i << ", lsl #3]\n";
      break;
    default:
      std::cerr << "cannot load at non 2-power scaled address" << std::endl;
    }
  }

  void StoreN(std::ostream& os, Register addr, int disp,
              Register value, unsigned bits) override {
    auto v = RegName(value, bits <= 32 ? 4 : 8);
    auto a = RegName(addr);
    if (bits <= 8) {
      os << "    strb " << v << ", [" << a << ", #" << disp << "]\n";
    } else if (bits <= 16) {
      os << "    strh " << v << ", [" << a << ", #" << disp << "]\n";
    } else if (bits <= 64) {
      os << "    str " << v << ", [" << a << ", #" << disp << "]\n";
    }
  }

  void StoreN(std::ostream& os, std::string_view sym_name,
              Register value, unsigned bits) override {
    os << "    adrp x10, _" << sym_name << "@PAGE\n";
    os << "    str " << RegName(value, (bits + 7) / 8)
       << ", [x10, _" << sym_name << "@PAGEOFF]\n";
  }

  void LoadPushN(std::ostream& os, Register addr, unsigned bits) override {
    if (bits <= 8) {
      os << "    ldrb w10, [" << RegName(addr) << "]\n";
    } else if (bits <= 16) {
      os << "    ldrh w10, [" << RegName(addr) << "]\n";
    } else if (bits <= 32) {
      os << "    ldr w10, [" << RegName(addr) << "]\n";
    } else if (bits <= 64) {
      os << "    ldr x10, [" << RegName(addr) << "]\n";
    } else {
      std::cerr << "cannot load more than 8 bytes" << std::endl;
    }
    os << "    str x10, [sp, #-16]!\n";
  }

  void LoadSymAddr(std::ostream& os, Register dest,
                   std::string_view sym_name) override {
    os << "    adrp " << RegName(dest) << ", _" << sym_name << "@GOTPAGE\n";
    os << "    ldr " << RegName(dest) << ", [" << RegName(dest)
       << ", _" << sym_name << "@GOTPAGEOFF]\n";
  }

  void Inc64(std::ostream& os, Register lhs) override {
    os << "    ldr x10, [" << RegName(lhs) << "]\n";
    os << "    add x10, x10, #1\n";
    os << "    str x10, [" << RegName(lhs) << "]\n";
  }

  void Dec64(std::ostream& os, Register lhs) override {
    os << "    ldr x10, [" << RegName(lhs) << "]\n";
    os << "    sub x10, x10, #1\n";
    os << "    str x10, [" << RegName(lhs) << "]\n";
  }

  void MaskBits(std::ostream& os, Register reg, unsigned bits) override {
    if (bits >= 64) {
      return;
    }
    os << "    ubfx " << RegName(reg) << ", " << RegName(reg)
       << ", #0, #" << bits << "\n";
  }

  void Jmp(std::ostream& os, std::string_view label) override {
    os << "    b " << label << "\n";
  }

  void JmpIfZero(std::ostream& os, Register reg,
                 std::string_view label) override {
    os << "    cbz " << RegName(reg) << ", " << label << "\n";
  }

  void JmpIfNotZero(std::ostream& os, Register reg,
                    std::string_view label) override {
    os << "    cbnz " << RegName(reg) << ", " << label << "\n";
  }

  void Call(std::ostream& os, Register addr) override {
    os << "    blr " << RegName(addr) << "\n";
  }

  void CmpSet(std::ostream& os, Compare c, Register dest,
              Register lhs, Register rhs) override {
    os << "    cmp " << RegName(lhs) << ", " << RegName(rhs) << "\n";
    os << "    cset " << RegName(dest) << ", ";
    switch (c) {
      case kCmpE:  os << "eq"; break;
      case kCmpNE: os << "ne"; break;
      case kCmpG:  os << "gt"; break;
      case kCmpLE: os << "le"; break;
      case kCmpA:  os << "hi"; break;
      case kCmpBE: os << "ls"; break;
    }
    os << "\n";
  }

  void FuncPrologue(std::ostream& os, Context* ctx) override {
    FuncPrologue(os, ctx->func_name);
    os << "    sub sp, sp, " << ((ctx->StackSize() + 15) & 0xfffffff0) << "\n";
    os << "    mov " << RegName(Asm::kRegL) << ", xzr\n";
  }

  void FuncPrologue(std::ostream& os, std::string_view sym_name) override {
    os << ".global _" << sym_name << "\n";
    os << ".p2align 2\n";
    os << '_' << sym_name << ":\n";
    os << "    stp x29, x30, [sp, #-16]!\n";
    os << "    mov x29, sp\n";
  }

  void FuncEpilogue(std::ostream& os, Context* ctx) override {
    os << "    ldr " << RegName(Asm::kRegRet) << ", [sp], #16\n";
    os << ctx->func_name << "_exit:\n";
    FuncEpilogue(os);
  }

  void FuncEpilogue(std::ostream& os) override {
    os << "    mov sp, x29\n";
    os << "    ldp x29, x30, [sp], #16\n";
    os << "    ret\n";
  }

  bool FuncVArgOnStack() override {
    return true;
  }

  void PrepareFuncVArg(std::ostream& os, size_t num_normal,
                       size_t num_variadic) override {
    for (size_t i = 1; i < num_variadic; ++i) {
      size_t src_disp = 16 * (num_normal + i);
      size_t dest_disp = 16 * num_normal + 8 * i;
      os << "    ldr x10, [sp, #" << src_disp << "]\n";
      os << "    str x10, [sp, #" << dest_disp << "]\n";
    }
  }

  void SectionText(std::ostream& os) override {
    os << ".section __TEXT,__text,regular,pure_instructions\n";
  }

  void SectionInit(std::ostream& os) override {
    os << ".section __DATA,__mod_init_func,mod_init_funcs\n";
    os << ".p2align 3\n";
  }

  void SectionData(std::ostream& os, bool readonly) override {
    os << ".section __DATA," << (readonly ? "__const\n" : "__data\n");
  }

  std::string SymLabel(std::string_view sym_name) override {
    return std::string{"_"}.append(sym_name);
  }
};
