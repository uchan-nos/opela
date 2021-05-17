#include "asm.hpp"

#include <array>
#include <iostream>
#include <string>

using namespace std;

class AsmX86_64 : public Asm {
 public:
  static constexpr std::array<const char*, kRegNum> kRegNames{
    "a", "di", "si", "d", "c", "r8", "r9",          // 戻り値、引数
    "r10", "r11", "di", "si", "d", "c", "r8", "r9", // 計算用
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

  void Mov64(Register dest, std::uint64_t v) {
    out_ << "    mov " << RegName(dest) << ',' << v << '\n';
  }

  void Mov64(Register dest, Register v) {
    out_ << "    mov " << RegName(dest) << ',' << RegName(v) << '\n';
  }

  void Add64(Register dest, Register v) {
    out_ << "    add " << RegName(dest) << ',' << RegName(v) << '\n';
  }

  void Sub64(Register dest, Register v) {
    out_ << "    sub " << RegName(dest) << ',' << RegName(v) << '\n';
  }
};

Asm* NewAsm(AsmArch arch, std::ostream& out) {
  switch (arch) {
  case AsmArch::kX86_64:
    return new AsmX86_64(out);
  }
  return nullptr;
}
