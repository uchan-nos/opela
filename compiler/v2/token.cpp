#include "token.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>

#include "magic_enum.hpp"
#include "source.hpp"

using namespace std;

namespace {

const map<Token::Kind, string> kKeywords{
  {Token::kRet,    "return"},
};

Token* NextToken(Source& src, const char* p) {
  while (p < src.End()) {
    if (isspace(*p)) {
      ++p;
      continue;
    }

    if (isdigit(*p)) {
      int base = 10;
      auto literal = p;

      if (*p == '0') {
        if (p[1] == 'b') {
          base = 2;
          literal = p + 2;
        } else {
          base = 8;
          literal = p + 1;
        }
      }

      char* non_digit;
      opela_type::Int v = strtol(literal, &non_digit, base);
      return new Token{Token::kInt, {p, non_digit}, v};
    }

    if (p[1] == '=' && strchr("=!<>:", p[0])) {
      return new Token{Token::kReserved, {p, 2}, {}};
    }

    if (string_view op{p, 2}; op == "||" || op == "&&") {
      return new Token{Token::kReserved, {p, 2}, {}};
    }

    if (strchr("+-*/()<>;{}", *p)) {
      return new Token{Token::kReserved, {p, 1}, {}};
    }

    bool match_keyword = false;
    for (auto& [ kind, name ] : kKeywords) {
      if (string_view raw{p, name.size()};
          raw == name && !isalpha(p[name.size()]) && p[name.size()] != '_') {
        return new Token{kind, raw, {}};
      }
    }

    if (isalpha(*p) || *p == '_') {
      auto id = p++;
      while (p < src.End() && (isalnum(*p) || *p == '_')) {
        ++p;
      }
      return new Token{Token::kId, {id, p}, {}};
    }

    cerr << "failed to tokenize" << endl;
    ErrorAt(src, p);
  }

  return new Token{Token::kEOF, {p, 0}, {}};
}

} // namespace

Tokenizer::Tokenizer(Source& src)
  : src_{src}, cur_token_{NextToken(src_, src_.Begin())} {
}

Token* Tokenizer::Peek() {
  return cur_token_;
}

Token* Tokenizer::Peek(Token::Kind kind) {
  if (cur_token_->kind == kind) {
   return cur_token_;
  }
  return nullptr;
}

Token* Tokenizer::Peek(std::string_view raw) {
  if (cur_token_->kind == Token::kReserved && cur_token_->raw == raw) {
    return cur_token_;
  }
  return nullptr;
}

Token* Tokenizer::Consume() {
  if (cur_token_->kind == Token::kEOF) {
    return cur_token_;
  }

  auto token = cur_token_;
  cur_token_ = NextToken(src_, cur_token_->raw.end());
  return token;
}

Token* Tokenizer::Consume(Token::Kind kind) {
  if (Peek(kind)) {
    return Consume();
  }
  return nullptr;
}

Token* Tokenizer::Consume(string_view raw) {
  if (Peek(raw)) {
    return Consume();
  }
  return nullptr;
}

Token* Tokenizer::Expect(Token::Kind kind) {
  if (auto token = Consume(kind)) {
    return token;
  }
  ::Unexpected(src_, *cur_token_);
}

Token* Tokenizer::Expect(string_view raw) {
  if (auto token = Consume(raw)) {
    return token;
  }
  ::Unexpected(src_, *cur_token_);
}

void Tokenizer::Unexpected(Token& token) {
  ::Unexpected(src_, token);
}

void ErrorAt(Source& src, Token& token) {
  ErrorAt(src, token.raw.begin());
}

void Unexpected(Source& src, Token& token) {
  cerr << "unexpected token "
    << magic_enum::enum_name(token.kind)
    << " '" << token.raw << "'\n";
  ErrorAt(src, token);
}

char GetEscapeValue(char escape_char) {
  switch (escape_char) {
  case '0': return 0;
  case 'a': return '\a';
  case 'b': return '\b';
  case 't': return '\t';
  case 'n': return '\n';
  case 'v': return '\v';
  case 'f': return '\f';
  case 'r': return '\r';
  default: return escape_char;
  }
}