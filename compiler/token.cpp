#include "token.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "magic_enum.hpp"
#include "source.hpp"

using namespace std;

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
        op == "==" || op == "!=" || op == "<=" || op == ">=") {
      Token tk{Token::kReserved, p, 2, 0};
      tokens.push_back(tk);
      p += 2;
      continue;
    }

    if (strchr("+-*/()<>", *p)) {
      Token tk{Token::kReserved, p, 1, 0};
      tokens.push_back(tk);
      ++p;
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
    << " '" << string(tk.loc, tk.len) << "'" << endl;
  ErrorAt(tk.loc);
}

bool Consume(Token::Kind kind) {
  if (cur_token->kind == kind) {
    ++cur_token;
    return true;
  }
  return false;
}

bool Consume(const string& raw) {
  string tk_raw(cur_token->loc, cur_token->len);
  if (cur_token->kind == Token::kReserved && tk_raw == raw) {
    ++cur_token;
    return true;
  }
  return false;
}

vector<Token>::iterator Expect(Token::Kind kind) {
  auto tk{cur_token};
  if (Consume(kind)) {
    return tk;
  }
  Error(*cur_token);
}

vector<Token>::iterator Expect(const string& raw) {
  auto tk{cur_token};
  if (Consume(raw)) {
    return tk;
  }
  Error(*cur_token);
}

bool AtEOF() {
  return cur_token->kind == Token::kEOF;
}
