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

    // 引数レジスタ
    kRegArg0, kRegArg1, kRegArg2, kRegArg3, kRegArg4, kRegArg5,

    // 引数レジスタと重複する可能性がある計算用レジスタ
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

  Asm(std::ostream& out) : out_{out} {}
  virtual ~Asm() = default;

  virtual void Mov64(Register dest, std::uint64_t v) = 0;
  virtual void Mov64(Register dest, Register v) = 0;
  virtual void Add64(Register dest, Register v) = 0;
  virtual void Sub64(Register dest, std::uint64_t v) = 0;
  virtual void Sub64(Register dest, Register v) = 0;
  virtual void Mul64(Register dest, Register v) = 0;
  virtual void Div64(Register dest, Register v) = 0;
  virtual void Push64(Register reg) = 0;
  virtual void Leave() = 0;
  virtual void Load64(Register dest, Register addr, int disp) = 0;
  virtual void Store64(Register addr, int disp, Register v) = 0;
  virtual void CmpSet(Compare c, Register dest, Register lhs, Register rhs) = 0;
  virtual void Xor64(Register dest, Register v) = 0;
  virtual void Ret() = 0;
  virtual void Jump(std::string_view label) = 0;

  // アーキテクチャ非依存な行を出力したいときに使う汎用出力メソッド
  std::ostream& Output() { return out_; }

 protected:
  std::ostream& out_;
};

enum class AsmArch {
  kX86_64,
};

Asm* NewAsm(AsmArch arch, std::ostream& out);
