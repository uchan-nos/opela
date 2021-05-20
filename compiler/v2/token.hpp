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
    kInt,
    kId,
  } kind;

  std::string_view raw;

  std::variant<opela_type::Int> value;
};

class Tokenizer {
 public:
  Tokenizer(Source& src);

  Token* Peek();
  Token* Consume();
  Token* Consume(Token::Kind kind);
  Token* Consume(std::string_view raw);
  Token* Expect(Token::Kind kind);
  Token* Expect(std::string_view raw);

  void Unexpected(Token& token);

 private:
  Source& src_;
  Token* cur_token_;
};

inline bool operator==(Token* token, Token::Kind kind) {
  return token ? token->kind == kind : false;
}

inline bool operator==(Token* token, std::string_view raw) {
  return token ? token->kind == Token::kReserved && token->raw == raw : false;
}

[[noreturn]] void ErrorAt(Source& src, Token& token);
[[noreturn]] void Unexpected(Source& src, Token& token);

// 与えられた文字に対応するエスケープ済み文字の ASCII コードを返す。
// 0, a, b, t, n, v, f, r 以外は、引数をそのまま返す。
//
// 例：
//   GetEscapeValue('n') == '\n'
//   GetEscapeValue('z') == 'z'
char GetEscapeValue(char escape_char);
