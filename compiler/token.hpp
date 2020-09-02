#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct Token {
  enum Kind {
    kOp,
    kInt,
    kEOF,
    kLParen,
    kRParen,
  } kind;

  const char* loc; // src の中を指すポインタ
  std::size_t len; // トークンの文字数

  std::int64_t value;
};

// 現在処理中のトークン
inline std::vector<Token>::iterator cur_token;

std::vector<Token> Tokenize(const char* p);
[[noreturn]] void Error(const Token& tk);
bool Consume(Token::Kind kind);
bool Consume(Token::Kind kind, const std::string& raw);
std::vector<Token>::iterator Expect(Token::Kind kind);
std::vector<Token>::iterator Expect(Token::Kind kind, const std::string& raw);
bool AtEOF();
