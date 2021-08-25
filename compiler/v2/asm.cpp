#include "asm.hpp"

#include <array>
#include <cstdarg>
#include <iostream>
#include <limits>
#include <string>

using namespace std;

class AsmX86_64 : public Asm {
 public:
  static constexpr std::array<const char*, kRegNum> kRegNames{
    "a", "di", "si", "d", "c", "r8", "r9", "r10", "r11", // 戻り値、引数、計算用
    "rbx", "r12", "r13", "r14", "r15",                   // 計算用（不揮発）
    "bp", "sp", "zero",
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

  using Asm::Asm;

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

  void Leave() override {
    PrintAsm(this, "    leave\n");
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
};

Asm* NewAsm(AsmArch arch, std::ostream& out) {
  switch (arch) {
  case AsmArch::kX86_64:
    return new AsmX86_64(out);
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
