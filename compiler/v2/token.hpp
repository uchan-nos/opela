#pragma once

#include <string_view>
#include <variant>
#include <vector>

#include "types.hpp"
#include "source.hpp"

struct Token {
  enum Kind {
    kEOF,
    kReserved,
    kInt,  // 整数リテラル
    kId,   // 識別子
    kStr,  // 文字列リテラル
    kChar, // 文字リテラル

    // キーワード
    kRet,
    kIf,
    kElse,
    kFor,
    kFunc,
    kExtern,
    kSizeof,
    kType,
    kVar,
    kBreak,
    kCont,
    kStruct,
  } kind;

  std::string_view raw;

  std::variant<opela_type::Int, opela_type::Byte> value;
};

class Tokenizer {
 public:
  Tokenizer(Source& src);

  Token* Peek();
  Token* Peek(Token::Kind kind);
  Token* Peek(std::string_view raw);
  Token* Consume();
  Token* Consume(Token::Kind kind);
  Token* Consume(std::string_view raw);
  Token* Expect(Token::Kind kind);
  Token* Expect(std::string_view raw);

  void Unexpected(Token& token);

  Token* SubToken(Token::Kind kind, std::size_t len);
  Token* ConsumeOrSub(std::string_view raw);

 private:
  Source& src_;
  Token* cur_token_;
};

[[noreturn]] void ErrorAt(Source& src, Token& token);
[[noreturn]] void Unexpected(Source& src, Token& token);

// 与えられた文字に対応するエスケープ済み文字の ASCII コードを返す。
// 0, a, b, t, n, v, f, r 以外は、引数をそのまま返す。
//
// 例：
//   GetEscapeValue('n') == '\n'
//   GetEscapeValue('z') == 'z'
char GetEscapeValue(char escape_char);
