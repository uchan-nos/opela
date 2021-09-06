#include "asm.hpp"

#include <array>
#include <cstdarg>
#include <iostream>
#include <limits>
#include <string>

#define NOT_IMPLEMENTED \
  do { \
    this->Output() << "; not implemented: " << __PRETTY_FUNCTION__ << std::endl; \
  } while (0)

using namespace std;

class AsmX86_64 : public Asm {
 public:
  static constexpr std::array<const char*, kRegNum> kRegNames{
    "a", "di", "si", "d", "c", "r8", "r9", "r10", "r11", // 戻り値、引数、計算用
    "rbx", "r12", "r13", "r14", "r15",                   // 計算用（不揮発）
    "bp", "sp", "zero", "", ""
  };
  static std::string RegName(std::string stem, DataType dt) {
    if (stem == "zero") {
      return "0";
    }
    if (stem.length() == 1) { // a, b, c, d
      switch (dt) {
      case kByte:  return stem + "l";
      case kWord:  return stem + "x";
      case kDWord: return "e" + stem + "x";
      case kQWord: return "r" + stem + "x";
      default: return "non-standard size";
      }
    }
    if (stem[0] == 'r') { // r8 - r15
      switch (dt) {
      case kByte:  return stem + "b";
      case kWord:  return stem + "w";
      case kDWord: return stem + "d";
      case kQWord: return stem;
      default: return "non-standard size";
      }
    }
    if (stem.length() == 2) { // di, si, bp, sp
      switch (dt) {
      case kByte:  return stem + "l";
      case kWord:  return stem;
      case kDWord: return "e" + stem;
      case kQWord: return "r" + stem;
      default: return "non-standard size";
      }
    }
    return "failed to get register name for " + stem;
  }

  static constexpr std::array<const char*, 5> kDataTypeName{
    "", "byte", "word", "dword", "qword"
  };

  using Asm::Asm; // コンストラクタ

  std::string RegName(Register reg, DataType dt = kQWord) override {
    return RegName(kRegNames[reg], dt);
  }

  bool SameReg(Register a, Register b) override {
    return RegName(a) == RegName(b);
  }

  void Mov64(Register dest, std::uint64_t v) override {
    if (v <= numeric_limits<uint32_t>::max()) {
      PrintAsm(this, "    mov %r32, %u64\n", dest, v);
    } else {
      PrintAsm(this, "    mov %r64, %u64\n", dest, v);
    }
  }

  void Mov64(Register dest, Register v) override {
    PrintAsm(this, "    mov %r64, %r64\n", dest, v);
  }

  void Add64(Register dest, std::uint64_t v) override {
    PrintAsm(this, "    add %r64, %u64\n", dest, v);
  }

  void Add64(Register dest, Register v) override {
    PrintAsm(this, "    add %r64, %r64\n", dest, v);
  }

  void Sub64(Register dest, std::uint64_t v) override {
    PrintAsm(this, "    sub %r64, %u64\n", dest, v);
  }

  void Sub64(Register dest, Register v) override {
    PrintAsm(this, "    sub %r64, %r64\n", dest, v);
  }

  void Mul64(Register dest, Register v) override {
    PrintAsm(this, "    imul %r64, %r64\n", dest, v);
  }

  void Mul64(Register dest, Register a, std::uint64_t b) override {
    PrintAsm(this, "    imul %r64, %r64, %u64\n", dest, a, b);
  }

  void Div64(Register dest, Register v) override {
    if (dest == kRegA) {
      PrintAsm(this, "    push rdx\n"
                     "    xor edx, edx\n"
                     "    div %r64\n"
                     "    pop rdx\n", v);
    } else {
      PrintAsm(this, "    push rax\n"
                     "    push rdx\n"
                     "    mov rax, %r64\n"
                     "    xor edx, edx\n"
                     "    div %r64\n"
                     "    mov %r64, rax\n"
                     "    pop rdx\n"
                     "    pop rax\n", dest, v, dest);
    }
  }

  void And64(Register dest, std::uint64_t v) override {
    PrintAsm(this, "    and %r64, %u64\n", dest, v);
  }

  void And64(Register dest, Register v) override {
    PrintAsm(this, "    and %r64, %r64\n", dest, v);
  }

  void Or64(Register dest, Register v) override {
    PrintAsm(this, "    or %r64, %r64\n", dest, v);
  }

  void Push64(Register reg) override {
    PrintAsm(this, "    push %r64\n", reg);
  }

  void Pop64(Register reg) override {
    PrintAsm(this, "    pop %r64\n", reg);
  }

  void LoadN(Register dest, Register addr, int disp, DataType dt) override {
    PrintAsm(this, "    mov %rm, %s ptr [%r64%i]\n",
             dest, dt, kDataTypeName[dt], addr, disp);
  }

  void LoadN(Register dest, std::string_view label, DataType dt) override {
    PrintAsm(this, "    mov %rm, %s ptr [rip+%S]\n",
             dest, dt, kDataTypeName[dt], label.data(), label.length());
  }

  void StoreN(Register addr, int disp, Register v, DataType dt) override {
    PrintAsm(this, "    mov %s ptr [%r64%i], %rm\n",
             kDataTypeName[dt], addr, disp, v, dt);
  }

  void StoreN(std::string_view label, Register v, DataType dt) override {
    PrintAsm(this, "    mov %s ptr [rip+%S], %rm\n",
             kDataTypeName[dt], label.data(), label.length(), v, dt);
  }

  void CmpSet(Compare c, Register dest, Register lhs, Register rhs) override {
    const char* cmp;
    switch (c) {
      case kCmpE:  cmp = "e"; break;
      case kCmpNE: cmp = "ne"; break;
      case kCmpG:  cmp = "g"; break;
      case kCmpLE: cmp = "le"; break;
      case kCmpA:  cmp = "a"; break;
      case kCmpBE: cmp = "be"; break;
    }
    PrintAsm(this, "    cmp %r64, %r64\n",  lhs, rhs);
    PrintAsm(this, "    set%s %r8\n",       cmp, dest);
    PrintAsm(this, "    movzx %r32, %r8\n", dest, dest);
  }

  void Xor64(Register dest, Register v) override {
    PrintAsm(this, "    xor %r64, %r64\n", dest, v);
  }

  void Ret() override {
    PrintAsm(this, "    ret\n");
  }

  void Jmp(std::string_view label) override {
    PrintAsm(this, "    jmp %S\n", label.data(), label.length());
  }

  void JmpIfZero(Register v, std::string_view label) override {
    PrintAsm(this, "    test %r64, %r64\n", v, v);
    PrintAsm(this, "    jz %S\n", label.data(), label.length());
  }

  void JmpIfNotZero(Register v, std::string_view label) override {
    PrintAsm(this, "    test %r64, %r64\n", v, v);
    PrintAsm(this, "    jnz %S\n", label.data(), label.length());
  }

  void LEA(Register dest, Register base, int disp) override {
    PrintAsm(this, "    lea %r64, [%r64%i]\n", dest, base, disp);
  }

  void Call(Register addr) override {
    PrintAsm(this, "    call %r64\n", addr);
  }

  void LoadLabelAddr(Register dest, std::string_view label) override {
    PrintAsm(this, "    movabs %r64, offset %S\n",
             dest, label.data(), label.length());
  }

  void Set1IfNonZero64(Register dest, Register v) override {
    PrintAsm(this, "    test %r64, %r64\n", v, v);
    PrintAsm(this, "    setnz %r8\n",       dest);
    PrintAsm(this, "    movzx %r32, %r8\n", dest, dest);
  }

  void ShiftL64(Register dest, int bits) override {
    PrintAsm(this, "    shl %r64, %i\n", dest, bits);
  }

  void ShiftR64(Register dest, int bits) override {
    PrintAsm(this, "    shr %r64, %i\n", dest, bits);
  }

  void ShiftAR64(Register dest, int bits) override {
    PrintAsm(this, "    sar %r64, %i\n", dest, bits);
  }

  void IncN(Register addr, DataType dt) override {
    PrintAsm(this, "    inc %s ptr [%r64]\n", kDataTypeName[dt], addr);
  }

  void DecN(Register addr, DataType dt) override {
    PrintAsm(this, "    dec %s ptr [%r64]\n", kDataTypeName[dt], addr);
  }

  void FilePrologue() override {
    PrintAsm(this,  ".intel_syntax noprefix\n");
  }

  void SectionText() override {
    PrintAsm(this, ".code64\n.section .text\n");
  }

  void SectionInit() override {
    PrintAsm(this, ".section .init_array\n");
  }

  void SectionData(bool readonly) override {
    PrintAsm(this, ".section %s\n", readonly ? ".rodata" : ".data");
  }

  std::string SymLabel(std::string_view sym_name) override {
    return std::string{sym_name};
  }

  void FuncPrologue(std::string_view sym_name) override {
    string sym_label{sym_name};
    PrintAsm(this, ".global %s\n", sym_label.c_str());
    PrintAsm(this, "%s:\n", sym_label.c_str());
    PrintAsm(this, "    push rbp\n");
    PrintAsm(this, "    mov rbp, rsp\n");
  }

  void FuncEpilogue() override {
    PrintAsm(this, "    leave\n");
    PrintAsm(this, "    ret\n");
  }

  bool VParamOnStack() override {
    return false;
  }
};

class AsmAArch64 : public Asm {
 public:
  static constexpr std::array<const char*, kRegNum> kRegNames{
    "0",
    "0", "1", "2", "3", "4", "5",
    "8", "9",
    "19", "20", "21", "22", "23",
    "29", "sp", "zr", "16", "17",
  };
  static std::string RegName(std::string stem, DataType dt) {
    if (stem == "sp") {
      return stem;
    }
    if (dt == kDWord) {
      return "w" + stem;
    } else if (dt == kQWord) {
      return "x" + stem;
    }
    return "failed to get register name for " + stem;
  }

  using Asm::Asm; // コンストラクタ

  std::string RegName(Register reg, DataType dt = kQWord) override {
    return RegName(kRegNames[reg], dt);
  }

  bool SameReg(Register a, Register b) override {
    return RegName(a) == RegName(b);
  }

  void Mov64(Register dest, std::uint64_t v) override {
    if (v <= 0xffffu) {
      PrintAsm(this, "    mov %r64, #%u64\n", dest, v);
      return;
    }

    for (int shift = 0; shift < 64; shift += 16) {
      if (uint16_t v16 = v >> shift; v16 != 0) {
        PrintAsm(this, "    %s %r64, #%u16, lsl #%u0\n",
                 shift == 0 ? "movz" : "movk", dest, v16, shift);
      }
    }
  }

  void Mov64(Register dest, Register v) override {
    PrintAsm(this, "    mov %r64, %r64\n", dest, v);
  }

  void Add64(Register dest, std::uint64_t v) override {
    PrintAsm(this, "    add %r64, %r64, #%u64\n", dest, dest, v);
  }

  void Add64(Register dest, Register v) override {
    PrintAsm(this, "    add %r64, %r64, %r64\n", dest, dest, v);
  }

  void Sub64(Register dest, std::uint64_t v) override {
    PrintAsm(this, "    sub %r64, %r64, #%u64\n", dest, dest, v);
  }

  void Sub64(Register dest, Register v) override {
    PrintAsm(this, "    sub %r64, %r64, %r64\n", dest, dest, v);
  }

  void Mul64(Register dest, Register v) override {
    PrintAsm(this, "    mul %r64, %r64, %r64\n", dest, dest, v);
  }

  void Mul64(Register dest, Register a, std::uint64_t b) override {
    Mov64(Asm::kRegScr0, b);
    PrintAsm(this, "    mul %r64, %r64, %r64\n", dest, a, Asm::kRegScr0);
  }

  void Div64(Register dest, Register v) override {
    PrintAsm(this, "    sdiv %r64, %r64, %r64\n", dest, dest, v);
  }

  void And64(Register dest, std::uint64_t v) override {
    Mov64(Asm::kRegScr0, v);
    PrintAsm(this, "    and %r64, %r64, %r64\n", dest, dest, Asm::kRegScr0);
  }

  void And64(Register dest, Register v) override {
    PrintAsm(this, "    and %r64, %r64, %r64\n", dest, dest, v);
  }

  void Or64(Register dest, Register v) override {
    PrintAsm(this, "    or %r64, %r64, %r64\n", dest, dest, v);
  }

  void Push64(Register reg) override {
    PrintAsm(this, "    str %r64, [sp, #-16]!\n", reg);
  }

  void Pop64(Register reg) override {
    PrintAsm(this, "    ldr %r64, [sp], #16\n", reg);
  }

  void LoadN(Register dest, Register addr, int disp, DataType dt) override {
    LoadStoreN("ldr", dest, addr, disp, dt);
  }

  void LoadN(Register dest, std::string_view label, DataType dt) override {
    LoadStoreN("ldr", dest, label, dt);
  }

  void StoreN(Register addr, int disp, Register v, DataType dt) override {
    LoadStoreN("str", v, addr, disp, dt);
  }

  void StoreN(std::string_view label, Register v, DataType dt) override {
    LoadStoreN("str", v, label, dt);
  }

  void CmpSet(Compare c, Register dest, Register lhs, Register rhs) override {
    const char* cmp;
    switch (c) {
      case kCmpE:  cmp = "eq"; break;
      case kCmpNE: cmp = "ne"; break;
      case kCmpG:  cmp = "gt"; break;
      case kCmpLE: cmp = "le"; break;
      case kCmpA:  cmp = "hi"; break;
      case kCmpBE: cmp = "ls"; break;
    }
    PrintAsm(this, "    cmp %r64, %r64\n", lhs, rhs);
    PrintAsm(this, "    cset %r64, %s\n",  dest, cmp);
  }

  void Xor64(Register dest, Register v) override {
    PrintAsm(this, "    eor %r64, %r64, %r64\n", dest, dest, v);
  }

  void Ret() override {
    PrintAsm(this, "    ret\n");
  }

  void Jmp(std::string_view label) override {
    PrintAsm(this, "    b %S\n", label.data(), label.length());
  }

  void JmpIfZero(Register v, std::string_view label) override {
    PrintAsm(this, "    cbz %r64, %S\n", v, label.data(), label.length());
  }

  void JmpIfNotZero(Register v, std::string_view label) override {
    PrintAsm(this, "    cbnz %r64, %S\n", v, label.data(), label.length());
  }

  void LEA(Register dest, Register base, int disp) override {
    PrintAsm(this, "    add %r64, %r64, #%i\n", dest, base, disp);
  }

  void Call(Register addr) override {
    PrintAsm(this, "    blr %r64\n", addr);
  }

  void LoadLabelAddr(Register dest, std::string_view label) override {
    PrintAsm(this, "    adrp %r64, %S@GOTPAGE\n",
             dest, label.data(), label.length());
    PrintAsm(this, "    ldr %r64, [%r64, %S@GOTPAGEOFF]\n",
             dest, dest, label.data(), label.length());
  }

  void Set1IfNonZero64(Register dest, Register v) override {
    PrintAsm(this, "    cmp %r64, %r64\n", v, Asm::kRegZero);
    PrintAsm(this, "    cset %r64, ne\n", dest);
  }

  void ShiftL64(Register dest, int bits) override {
    PrintAsm(this, "    lsl %r64, %r64, #%i\n", dest, dest, bits);
  }

  void ShiftR64(Register dest, int bits) override {
    PrintAsm(this, "    lsr %r64, %r64, #%i\n", dest, dest, bits);
  }

  void ShiftAR64(Register dest, int bits) override {
    PrintAsm(this, "    asr %r64, %r64, #%i\n", dest, dest, bits);
  }

  void IncN(Register addr, DataType dt) override {
    LoadN(Asm::kRegScr0, addr, 0, dt);
    PrintAsm(this, "    add x16, x16, #1\n", addr);
    StoreN(addr, 0, Asm::kRegScr0, dt);
  }

  void DecN(Register addr, DataType dt) override {
    LoadN(Asm::kRegScr0, addr, 0, dt);
    PrintAsm(this, "    sub x16, x16, #1\n", addr);
    StoreN(addr, 0, Asm::kRegScr0, dt);
  }

  void FilePrologue() override {
  }

  void SectionText() override {
    PrintAsm(this, ".section __TEXT,__text,regular,pure_instructions\n");
  }

  void SectionInit() override {
    PrintAsm(this, ".section __DATA,__mod_init_func,mod_init_funcs\n");
    PrintAsm(this, ".p2align 3\n");
  }

  void SectionData(bool readonly) override {
    PrintAsm(this, ".section __DATA,%s\n", readonly ? "__const" : "__data");
  }

  std::string SymLabel(std::string_view sym_name) override {
    return std::string{"_"}.append(sym_name);
  }

  void FuncPrologue(std::string_view sym_name) override {
    auto sym_label = SymLabel(sym_name);
    PrintAsm(this, ".global %s\n", sym_label.c_str());
    PrintAsm(this, ".p2align 2\n");
    PrintAsm(this, "%s:\n", sym_label.c_str());
    PrintAsm(this, "    stp x29, x30, [sp, #-16]!\n");
    PrintAsm(this, "    mov x29, sp\n");
  }

  void FuncEpilogue() override {
    PrintAsm(this, "    mov sp, x29\n");
    PrintAsm(this, "    ldp x29, x30, [sp], #16\n");
    PrintAsm(this, "    ret\n");
  }

  bool VParamOnStack() override {
    return true;
  }

 private:
  void LoadStoreN(const char* inst,
                  Register v, Register addr, int disp, DataType dt) {
    const char* fmt;
    switch (dt) {
      case kByte:  fmt = "    %sb %r32, [%r64, #%i]\n"; break;
      case kWord:  fmt = "    %sh %r32, [%r64, #%i]\n"; break;
      case kDWord: fmt = "    %s %r32, [%r64, #%i]\n"; break;
      case kQWord: fmt = "    %s %r64, [%r64, #%i]\n"; break;
      default:     fmt = "non-standard size is not supported\n";
    }
    PrintAsm(this, fmt, inst, v, addr, disp);
  }

  void LoadStoreN(const char* inst,
                  Register v, std::string_view label, DataType dt) {
    PrintAsm(this, "    adrp x16, _%S@PAGE\n", label.data(), label.length());
    const char* fmt;
    switch (dt) {
      case kByte:  fmt = "    %sb %r32, [x16, _%S@PAGEOFF]\n"; break;
      case kWord:  fmt = "    %sh %r32, [x16, _%S@PAGEOFF]\n"; break;
      case kDWord: fmt = "    %s %r32, [x16, _%S@PAGEOFF]\n"; break;
      case kQWord: fmt = "    %s %r64, [x16, _%S@PAGEOFF]\n"; break;
      default:     fmt = "non-standard size is not supported\n";
    }
    PrintAsm(this, fmt, inst, v, label.data(), label.length());
  }
};

Asm* NewAsm(AsmArch arch, std::ostream& out) {
  switch (arch) {
  case AsmArch::kX86_64:
    return new AsmX86_64(out);
  case AsmArch::kAArch64:
    return new AsmAArch64(out);
  }
  return nullptr;
}

void PrintAsm(Asm* asmgen, const char* format, ...) {
  va_list args;
  va_start(args, format);

  const char* p = format;
  auto& out = asmgen->Output();

  auto read_bits = [&]{
    long bits = strtol(p, NULL, 10);
    if (bits == 0) {
      // pass
    } else if (bits < 10) {
      p++;
    } else if (bits < 100) {
      p += 2;
    } else if (bits < 1000) {
      p += 3;
    }
    return bits;
  };

  while (*p) {
    if (*p != '%') {
      out << *p;
      p++;
      continue;
    }

    p++;
    const char c = *p;
    if (c == 'r') { // register
      auto r = static_cast<Asm::Register>(va_arg(args, int));
      p++;
      if (*p == 'm') {
        auto dt = static_cast<Asm::DataType>(va_arg(args, int));
        out << asmgen->RegName(r, dt);
        p++;
      } else if (isdigit(*p)) {
        long bits = read_bits();
        out << asmgen->RegName(r, BitsToDataType(bits));
      }
    } else if (c == 'i') { // signed value
      p++;
      long bits = read_bits();
      int64_t v = 0;
      if (bits <= 16) {
        v = va_arg(args, int);
      } else if (bits == 32) {
        v = va_arg(args, int32_t);
      } else if (bits == 64) {
        v = va_arg(args, int64_t);
      }
      out << showpos << v << noshowpos;
    } else if (c == 'u') { // unsigned value
      p++;
      long bits = read_bits();
      uint64_t v = 0;
      if (bits <= 16) {
        v = va_arg(args, int);
      } else if (bits == 32) {
        v = va_arg(args, uint32_t);
      } else if (bits == 64) {
        v = va_arg(args, uint64_t);
      }
      out << v;
    } else if (c == 's') {
      p++;
      auto s = va_arg(args, const char*);
      out << s;
    } else if (c == 'S') {
      p++;
      auto s = va_arg(args, const char*);
      auto l = va_arg(args, size_t);
      out << string_view(s, l);
    }
  }

  va_end(args);
}
