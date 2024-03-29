#pragma once

#include <bitset>
#include <cstdint>
#include <ostream>
#include <string_view>

class Asm {
 public:
  enum Register {
    // **************************
    // **** 揮発性レジスタ ****
    // **************************

    // 戻り値、演算結果、計算用レジスタ
    kRegA,

    // 引数、計算用レジスタ
    kRegV0, kRegV1, kRegV2, kRegV3, kRegV4, kRegV5,

    // 引数および戻り値レジスタと重複しない計算用レジスタ
    kRegX, kRegY,

    // **************************
    // **** 不揮発性レジスタ ****
    // **************************

    // 計算用レジスタ
    kRegNV0, kRegNV1, kRegNV2, kRegNV3, kRegNV4,

    // フレームポインタとスタックポインタ
    kRegBP, kRegSP,

    kRegZero, // ゼロレジスタ

    kRegScr0, kRegScr1, // スクラッチレジスタ

    kRegNum,  // 列挙子の数
  };
  using RegSet = std::bitset<kRegNum>;

  enum Compare {
    kCmpE,
    kCmpNE,
    kCmpG,  // a > b (signed)
    kCmpLE, // a <= b (signed)
    kCmpA,  // a > b (unsigned)
    kCmpBE, // a <= b (unsigned)
  };

  enum DataType {
    kNonStandardDataType, kByte, kWord, kDWord, kQWord
  };

  Asm(std::ostream& out) : out_{out} {}
  virtual ~Asm() = default;

  virtual std::string RegName(Register reg, DataType dt = kQWord) = 0;
  virtual bool SameReg(Register a, Register b) = 0;

  virtual void Mov64(Register dest, std::uint64_t v) = 0;
  virtual void Mov64(Register dest, Register v) = 0;
  virtual void Add64(Register dest, std::uint64_t) = 0;
  virtual void Add64(Register dest, Register v) = 0;
  virtual void Sub64(Register dest, std::uint64_t v) = 0;
  virtual void Sub64(Register dest, Register v) = 0;
  virtual void Mul64(Register dest, Register v) = 0;
  virtual void Mul64(Register dest, Register a, std::uint64_t b) = 0;
  virtual void Div64(Register dest, Register v) = 0;
  virtual void And64(Register dest, std::uint64_t v) = 0;
  virtual void And64(Register dest, Register v) = 0;
  virtual void Or64(Register dest, Register v) = 0;
  virtual void Push64(Register reg) = 0;
  virtual void Pop64(Register reg) = 0;
  virtual void LoadN(Register dest, Register addr, int disp, DataType dt) = 0;
  virtual void LoadN(Register dest, std::string_view label, DataType dt) = 0;
  virtual void StoreN(Register addr, int disp, Register v, DataType dt) = 0;
  virtual void StoreN(std::string_view label, Register v, DataType dt) = 0;
  virtual void CmpSet(Compare c, Register dest, Register lhs, Register rhs) = 0;
  virtual void Xor64(Register dest, Register v) = 0;
  virtual void Ret() = 0;
  virtual void Jmp(std::string_view label) = 0;
  virtual void JmpIfZero(Register v, std::string_view label) = 0;
  virtual void JmpIfNotZero(Register v, std::string_view label) = 0;
  virtual void LEA(Register dest, Register base, int disp) = 0;
  virtual void Call(Register addr) = 0;
  virtual void LoadLabelAddr(Register dest, std::string_view label) = 0;
  virtual void Set1IfNonZero64(Register dest, Register v) = 0;
  virtual void ShiftL64(Register dest, int bits) = 0;
  virtual void ShiftR64(Register dest, int bits) = 0;
  virtual void ShiftAR64(Register dest, int bits) = 0;
  virtual void IncN(Register addr, DataType dt) = 0;
  virtual void DecN(Register addr, DataType dt) = 0;

  virtual void FilePrologue() = 0;
  virtual void SectionText() = 0;
  virtual void SectionInit() = 0;
  virtual void SectionData(bool readonly) = 0;
  virtual std::string SymLabel(std::string_view sym_name) = 0;
  virtual void FuncPrologue(std::string_view sym_name) = 0;
  virtual void FuncEpilogue() = 0;
  virtual bool VParamOnStack() = 0;

  // アーキテクチャ非依存な行を出力したいときに使う汎用出力メソッド
  std::ostream& Output() { return out_; }

 protected:
  std::ostream& out_;
};

enum class AsmArch {
  kX86_64,
  kAArch64,
};

Asm* NewAsm(AsmArch arch, std::ostream& out);

constexpr Asm::DataType BitsToDataType(int bits) {
  switch (bits) {
  case 8: return Asm::kByte;
  case 16: return Asm::kWord;
  case 32: return Asm::kDWord;
  case 64: return Asm::kQWord;
  }
  return Asm::kNonStandardDataType;
}

void PrintAsm(Asm* asmgen, const char* format, ...);
