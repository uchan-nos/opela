#include "asm.hpp"

#include <array>
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
  static std::string RegName(Register reg, DataType dt = kQWord) {
    return RegName(kRegNames[reg], dt);
  }

  using Asm::Asm;

  bool SameReg(Register a, Register b) override {
    return RegName(a) == RegName(b);
  }

  void Mov64(Register dest, std::uint64_t v) override {
    if (v <= numeric_limits<uint32_t>::max()) {
      out_ << "    mov " << RegName(dest, kDWord) << ',' << v << '\n';
    } else {
      out_ << "    mov " << RegName(dest) << ',' << v << '\n';
    }
  }

  void Mov64(Register dest, Register v) override {
    out_ << "    mov " << RegName(dest) << ',' << RegName(v) << '\n';
  }

  void Add64(Register dest, Register v) override {
    out_ << "    add " << RegName(dest) << ',' << RegName(v) << '\n';
  }

  void Sub64(Register dest, std::uint64_t v) override {
    out_ << "    sub " << RegName(dest) << ',' << v << '\n';
  }

  void Sub64(Register dest, Register v) override {
    out_ << "    sub " << RegName(dest) << ',' << RegName(v) << '\n';
  }

  void Mul64(Register dest, Register v) override {
    out_ << "    imul " << RegName(dest) << ',' << RegName(v) << '\n';
  }

  void Mul64(Register dest, Register a, std::uint64_t b) override {
    out_ << "    imul " << RegName(dest) << ','
         << RegName(a) << ',' << b << '\n';
  }

  void Div64(Register dest, Register v) override {
    if (dest == kRegA) {
      out_ << "    push rdx\n"
              "    xor edx, edx\n"
              "    div " << RegName(v) << "\n"
              "    pop rdx\n";
    } else {
      out_ << "    push rax\n"
              "    push rdx\n"
              "    mov rax, " << RegName(dest) << "\n"
              "    xor rdx, rdx\n"
              "    div " << RegName(v) << "\n"
              "    mov " << RegName(dest) << ", rax\n"
              "    pop rdx\n"
              "    pop rax\n";
    }
  }

  void And64(Register dest, std::uint64_t v) override {
    out_ << "    and " << RegName(dest) << ',' << v << '\n';
  }

  void And64(Register dest, Register v) override {
    out_ << "    and " << RegName(dest) << ',' << RegName(v) << '\n';
  }

  void Or64(Register dest, Register v) override {
    out_ << "    or " << RegName(dest) << ',' << RegName(v) << '\n';
  }

  void Push64(Register reg) override {
    out_ << "    push " << RegName(reg) << '\n';
  }

  void Pop64(Register reg) override {
    out_ << "    pop " << RegName(reg) << '\n';
  }

  void Leave() override {
    out_ << "    leave\n";
  }

  void Load64(Register dest, Register addr, int disp) override {
    out_ << "    mov " << RegName(dest)
         << ",[" << RegName(addr) << (disp >= 0 ? "+" : "") << disp << "]\n";
  }

  void Load64(Register dest, std::string_view label) override {
    out_ << "    mov " << RegName(dest) << ",[rip+" << label << "]\n";
  }

  void Store64(Register addr, int disp, Register v) override {
    StoreN(addr, disp, v, kQWord);
  }

  void Store64(std::string_view label, Register v) override {
    out_ << "    mov [rip+" << label << "]," << RegName(v) << '\n';
  }

  void StoreN(Register addr, int disp, Register v, DataType dt) override {
    out_ << "    mov [" << RegName(addr) << (disp >= 0 ? "+" : "") << disp
         << "]," << RegName(v, dt) << '\n';
  }

  void CmpSet(Compare c, Register dest, Register lhs, Register rhs) override {
    out_ << "    cmp " << RegName(lhs) << ',' << RegName(rhs) << '\n';
    out_ << "    set";
    switch (c) {
      case kCmpE:  out_ << "e"; break;
      case kCmpNE: out_ << "ne"; break;
      case kCmpG:  out_ << "g"; break;
      case kCmpLE: out_ << "le"; break;
      case kCmpA:  out_ << "a"; break;
      case kCmpBE: out_ << "be"; break;
    }
    out_ << ' ' << RegName(dest, kByte) << '\n';
    out_ << "    movzx " << RegName(dest, kDWord) << ','
         << RegName(dest, kByte) << '\n';
  }

  void Xor64(Register dest, Register v) override {
    out_ << "    xor " << RegName(dest) << ',' << RegName(v) << '\n';
  }

  void Ret() override {
    out_ << "    ret\n";
  }

  void Jmp(std::string_view label) override {
    out_ << "    jmp " << label << '\n';
  }

  void JmpIfZero(Register v, std::string_view label) override {
    out_ << "    test " << RegName(v) << ',' << RegName(v) << '\n';
    out_ << "    jz " << label << '\n';
  }

  void JmpIfNotZero(Register v, std::string_view label) override {
    out_ << "    test " << RegName(v) << ',' << RegName(v) << '\n';
    out_ << "    jnz " << label << '\n';
  }

  void LEA(Register dest, Register base, int disp) override {
    out_ << "    lea " << RegName(dest)
         << ", [" << RegName(base)
         << (disp >= 0 ? "+" : "") << disp << "]\n";
  }

  void Call(Register addr) override {
    out_ << "    call " << RegName(addr) << '\n';
  }

  void LoadLabelAddr(Register dest, std::string_view label) override {
    out_ << "    movabs " << RegName(dest) << ",offset " << label << '\n';
  }

  void Set1IfNonZero64(Register dest, Register v) override {
    out_ << "    test " << RegName(v) << ',' << RegName(v) << '\n'
         << "    setnz " << RegName(dest, kByte) << '\n'
         << "    movzx " << RegName(dest) << ',' << RegName(dest, kByte) << '\n';
  }

  virtual void ShiftL64(Register dest, int bits) override {
    out_ << "    shl " << RegName(dest) << ',' << bits << '\n';
  }

  virtual void ShiftR64(Register dest, int bits) override {
    out_ << "    shr " << RegName(dest) << ',' << bits << '\n';
  }

  virtual void ShiftAR64(Register dest, int bits) override {
    out_ << "    sar " << RegName(dest) << ',' << bits << '\n';
  }
};

Asm* NewAsm(AsmArch arch, std::ostream& out) {
  switch (arch) {
  case AsmArch::kX86_64:
    return new AsmX86_64(out);
  }
  return nullptr;
}
