#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct Token {
  enum Kind {
    kReserved,
    kInt,
    kEOF,
    kId,
    kRet,
    kIf,
    kFor,
  } kind;

  const char* loc; // src の中を指すポインタ
  std::size_t len; // トークンの文字数

  std::int64_t value;

  std::string Raw() const { return {loc, len}; }
};

// 現在処理中のトークン
inline std::vector<Token>::iterator cur_token;

std::vector<Token> Tokenize(const char* p);
[[noreturn]] void Error(const Token& tk);
Token* Consume(Token::Kind kind);
Token* Consume(const std::string& raw);
Token* Expect(Token::Kind kind);
Token* Expect(const std::string& raw);
bool AtEOF();
void Rewind();