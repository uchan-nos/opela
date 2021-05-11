#pragma once

#include <istream>
#include <string>
#include <utility>

// コンパイル対象のソースコード全体
class Source {
 public:
  // src にソースコードを読み込む
  void ReadAll(std::istream& is);

  // 指定された箇所を含む行を取得する
  std::string_view GetLine(const char* loc);

  const char* Begin() { return &*src_.begin(); }
  const char* End() { return &*src_.end() - 1; }

 private:
  std::string src_; // ファイルの中身全体（末尾はヌル文字）
};

// 指定された箇所をエラー表示し、プログラムを終了する
[[noreturn]] void ErrorAt(Source& src, const char* loc);
