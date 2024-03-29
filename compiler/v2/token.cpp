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
  {Token::kIf,     "if"},
  {Token::kElse,   "else"},
  {Token::kFor,    "for"},
  {Token::kFunc,   "func"},
  {Token::kExtern, "extern"},
  {Token::kSizeof, "sizeof"},
  {Token::kType,   "type"},
  {Token::kVar,    "var"},
  {Token::kBreak,  "break"},
  {Token::kCont,   "continue"},
  {Token::kStruct, "struct"},
};

const char* FindStr(const char* p) {
  if (*p != '"') {
    return nullptr;
  }

  ++p;
  while (*p && *p != '"') {
    if (*p == '\\') {
      p += 2;
    } else {
      ++p;
    }
  }

  if (*p == '"') {
    ++p;
  }
  return p;
}

Token* NextToken(Source& src, const char* p) {
  while (p < src.End()) {
    if (isspace(*p)) {
      ++p;
      continue;
    }

    if (string_view comment{p, 2}; comment == "//") {
      if (auto end_comment = strchr(p + 2, '\n')) {
        p = end_comment + 1;
        continue;
      }
      p = src.End();
      break;
    } else if (comment == "/*") {
      if (auto end_comment = strstr(p + 2, "*/")) {
        p = end_comment + 2;
        continue;
      }
      p = src.End();
      break;
    }

    if (isdigit(*p)) {
      int base = 10;
      auto literal = p;

      if (*p == '0') {
        if (p[1] == 'b') {
          base = 2;
          literal = p + 2;
        } else if (p[1] == 'x') {
          base = 16;
          literal = p + 2;
        } else {
          base = 8;
          literal = p + 1;
        }
      }

      char* non_digit;
      opela_type::Int v = strtol(literal, &non_digit, base);
      return new Token{Token::kInt, {p, static_cast<size_t>(non_digit - p)}, v};
    }

    if (string_view op{p, 3}; op == "...") {
      return new Token{Token::kReserved, {p, 3}, {}};
    }

    if (p[1] == '=' && strchr("=!<>:+-*/", p[0])) {
      return new Token{Token::kReserved, {p, 2}, {}};
    }

    if (string_view op{p, 2};
        op == "||" || op == "&&" || op == "++" || op == "--" || op == "->") {
      return new Token{Token::kReserved, {p, 2}, {}};
    }

    if (strchr("+-*/()<>;{}=,@&[].", *p)) {
      return new Token{Token::kReserved, {p, 1}, {}};
    }

    for (auto& [ kind, name ] : kKeywords) {
      if (string_view raw{p, name.size()};
          raw == name && !isalnum(p[name.size()]) && p[name.size()] != '_') {
        return new Token{kind, raw, {}};
      }
    }

    if (isalpha(*p) || *p == '_') {
      auto id = p++;
      while (p < src.End() && (isalnum(*p) || *p == '_')) {
        ++p;
      }
      return new Token{Token::kId, {id, static_cast<size_t>(p - id)}, {}};
    }

    if (*p == '"') {
      auto str_end = FindStr(p);
      if (*str_end == '\0') {
        cerr << "incomplete string literal" << endl;
        ErrorAt(src, p);
      }
      return new Token{Token::kStr, {p, static_cast<size_t>(str_end - p)}, {}};
    }

    if (*p == '\'') {
      if (p[1] != '\\' && p[2] == '\'') {
        return new Token{Token::kChar, {p, 3}, opela_type::Byte(p[1])};
      } else if (p[1] == '\\' && p[3] == '\'') {
        char v = GetEscapeValue(p[2]);
        return new Token{Token::kChar, {p, 4}, opela_type::Byte(v)};
      }
      cerr << "invalid char literal" << endl;
      ErrorAt(src, p);
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

Token* Tokenizer::SubToken(Token::Kind kind, std::size_t len) {
  auto sub_token = cur_token_->raw.substr(0, len);
  cur_token_->raw = cur_token_->raw.substr(len);
  return new Token{kind, sub_token, {}};
}

Token* Tokenizer::ConsumeOrSub(std::string_view raw) {
  if (auto token = Consume(raw)) {
    return token;
  } else if (cur_token_->kind == Token::kReserved &&
             cur_token_->raw.starts_with(raw)) {
    return SubToken(Token::kReserved, raw.length());
  }
  return nullptr;
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
