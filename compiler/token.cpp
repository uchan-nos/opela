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
  {Token::kRet, "return"},
  {Token::kIf,  "if"},
  {Token::kFor, "for"},
};

bool IsAlpha(char ch) {
  return isalpha(ch) || ch == '_';
}

bool IsAlNum(char ch) {
  return isalnum(ch) || ch == '_';
}

} // namespace

vector<Token> Tokenize(const char* p) {
  vector<Token> tokens;

  while (*p) {
    if (isspace(*p)) {
      ++p;
      continue;
    }

    if (isdigit(*p)) {
      char* non_digit;
      long v{strtol(p, &non_digit, 0)};
      size_t len = non_digit - p;
      Token tk{Token::kInt, p, len, v};
      tokens.push_back(tk);
      p = non_digit;
      continue;
    }

    if (string op{p, 2};
        op == "==" || op == "!=" || op == "<=" || op == ">=" || op == ":=") {
      Token tk{Token::kReserved, p, 2, 0};
      tokens.push_back(tk);
      p += 2;
      continue;
    }

    if (strchr("+-*/()<>;{}=", *p)) {
      Token tk{Token::kReserved, p, 1, 0};
      tokens.push_back(tk);
      ++p;
      continue;
    }

    bool match_keyword{false};
    for (auto [ kind, name ] : kKeywords) {
      if (strncmp(p, name.c_str(), name.size()) == 0 &&
          !IsAlNum(p[name.size()])) {
        tokens.push_back(Token{kind, p, name.size(), 0});
        p += name.size();
        match_keyword = true;
        break;
      }
    }
    if (match_keyword) {
      continue;
    }

    if (IsAlpha(*p)) {
      auto id{p++};
      while (IsAlNum(*p)) {
        ++p;
      }
      size_t len = p - id;
      Token tk{Token::kId, id, len, 0};
      tokens.push_back(tk);
      continue;
    }

    cerr << "failed to tokenize" << endl;
    ErrorAt(p);
  }

  tokens.push_back(Token{Token::kEOF, p, 0, 0});
  return tokens;
}

void Error(const Token& tk) {
  cerr << "unexpected token "
    << magic_enum::enum_name(tk.kind)
    << " '" << tk.Raw() << "'" << endl;
  ErrorAt(tk.loc);
}

Token* Consume(Token::Kind kind) {
  if (cur_token->kind == kind) {
    auto tk{cur_token};
    ++cur_token;
    return &*tk;
  }
  return nullptr;
}

Token* Consume(const string& raw) {
  string tk_raw(cur_token->loc, cur_token->len);
  if (cur_token->kind == Token::kReserved && tk_raw == raw) {
    auto tk{cur_token};
    ++cur_token;
    return &*tk;
  }
  return nullptr;
}

Token* Expect(Token::Kind kind) {
  if (auto tk = Consume(kind)) {
    return tk;
  }
  Error(*cur_token);
}

Token* Expect(const string& raw) {
  if (auto tk = Consume(raw)) {
    return tk;
  }
  Error(*cur_token);
}

bool AtEOF() {
  return cur_token->kind == Token::kEOF;
}

void Rewind() {
  --cur_token;
}