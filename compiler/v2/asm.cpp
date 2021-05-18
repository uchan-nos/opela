#include "asm.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <string>

using namespace std;

class AsmX86_64 : public Asm {
 public:
  static constexpr std::array<const char*, kRegNum> kRegNames{
    "a", "di", "si", "d", "c", "r8", "r9",          // 戻り値、引数
    "di", "si", "d", "c", "r8", "r9", "r10", "r11", // 計算用
    "rbx", "r12", "r13", "r14", "r15",              // 計算用（不揮発）
    "bp", "sp", "zero",
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

  using Asm::Asm;

  void Mov64(Register dest, std::uint64_t v) override {
    if (v <= numeric_limits<uint32_t>::max()) {
      out_ << "    mov " << RegName(dest, 4) << ',' << v << '\n';
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

  void Sub64(Register dest, Register v) override {
    out_ << "    sub " << RegName(dest) << ',' << RegName(v) << '\n';
  }

  void Mul64(Register dest, Register v) override {
    if (dest == kRegA) {
      out_ << "    push rdx\n"
              "    mul " << RegName(v) << "\n"
              "    pop rdx\n";
    } else {
      out_ << "    push rax\n"
              "    push rdx\n"
              "    mov rax, " << RegName(dest) << "\n"
              "    mul " << RegName(v) << "\n"
              "    mov " << RegName(dest) << ", rax\n"
              "    pop rdx\n"
              "    pop rax\n";
    }
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
    out_ << ' ' << RegName(dest, 1) << '\n';
    out_ << "    movzx " << RegName(dest, 4) << ',' << RegName(dest, 1) << '\n';
  }
};

Asm* NewAsm(AsmArch arch, std::ostream& out) {
  switch (arch) {
  case AsmArch::kX86_64:
    return new AsmX86_64(out);
  }
  return nullptr;
}
