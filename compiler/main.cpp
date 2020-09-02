#include <cctype>
#include <cstdint>
#include <iostream>
#include <list>
#include <string>

#include "magic_enum.hpp"

using namespace std;

struct Token {
  enum Kind {
    kOperator,
    kInteger,
    kEOF,
  };

  Kind kind;
  int64_t value;
  string raw;
};

// 現在処理中のトークン
list<Token>::iterator cur_token;

bool Consume(Token::Kind kind, const string& raw) {
  if (cur_token->kind == kind && cur_token->raw == raw) {
    ++cur_token;
    return true;
  }
  return false;
}

list<Token>::iterator Expect(Token::Kind kind) {
  if (cur_token->kind == kind) {
    return cur_token++;
  }
  cerr << "unexpected token "
    << magic_enum::enum_name(cur_token->kind) << endl;
  exit(1);
}

list<Token>::iterator Expect(Token::Kind kind, const string& raw) {
  if (cur_token->kind == kind && cur_token->raw == raw) {
    return cur_token++;
  }
  cerr << "unexpected token "
    << magic_enum::enum_name(cur_token->kind)
    << " '" << cur_token->raw << "'" << endl;
  exit(1);
}

bool AtEOF() {
  return cur_token->kind == Token::kEOF;
}

auto Tokenize(const char* p) {
  list<Token> tokens;

  while (*p) {
    if (isspace(*p)) {
      ++p;
      continue;
    }

    if (*p == '+' || *p == '-') {
      Token tk{Token::kOperator, INT64_C(0), string(p, 1)};
      tokens.push_back(tk);
      ++p;
      continue;
    }

    if (isdigit(*p)) {
      auto non_digit{p};
      long v{strtol(p, const_cast<char**>(&non_digit), 0)};
      Token tk{Token::kInteger, v, string(p, non_digit - p)};
      tokens.push_back(tk);
      p = non_digit;
      continue;
    }

    cerr << "failed to tokenize" << endl;
    exit(1);
  }

  tokens.push_back(Token{Token::kEOF, 0, "\0"});
  return tokens;
}

int main() {
  string line;
  getline(cin, line, '\n');
  auto tokens{Tokenize(line.c_str())};
  cur_token = tokens.begin();

  cout << "bits 64\nsection .text\n";
  cout << "global main\n";
  cout << "main:\n";

  cout << "    mov rax, " << Expect(Token::kInteger)->value << "\n";
  while (!AtEOF()) {
    if (Consume(Token::kOperator, "+")) {
      cout << "    add rax, " << Expect(Token::kInteger)->value << "\n";
      continue;
    }

    Expect(Token::kOperator, "-");
    cout << "    sub rax, " << Expect(Token::kInteger)->value << "\n";
  }

  cout << "    ret\n";
}
