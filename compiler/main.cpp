#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <list>
#include <string>
#include <utility>
#include <vector>

#include "magic_enum.hpp"

using namespace std;

struct Token {
  enum Kind {
    kOperator,
    kInteger,
    kEOF,
  };

  Kind kind;
  const char* loc; // src の中を指すポインタ
  size_t len; // トークンの文字数
  int64_t value;
};

// コンパイル対象のソースコード全体
vector<char> src;

// 現在処理中のトークン
list<Token>::iterator cur_token;

pair<const char*, const char*> FindLine(const char* p) {
  auto head{p};
  while (&src[0] < head && head[-1] != '\n') {
    --head;
  }
  while (*p != '\n' && *p != '\0') {
    ++p;
  }
  return {head, p};
}

[[noreturn]] void ErrorAt(const char* loc) {
  auto [ line, line_end ] = FindLine(loc);
  cerr << string(line, line_end - line) << endl;
  cerr << string(loc - line, ' ') << '^' << endl;
  exit(1);
}

[[noreturn]] void Error(const Token& tk) {
  cerr << "unexpected token "
    << magic_enum::enum_name(tk.kind)
    << " '" << string(tk.loc, tk.len) << "'" << endl;
  ErrorAt(tk.loc);
}

bool Consume(Token::Kind kind, const string& raw) {
  string tk_raw(cur_token->loc, cur_token->len);
  if (cur_token->kind == kind && tk_raw == raw) {
    ++cur_token;
    return true;
  }
  return false;
}

list<Token>::iterator Expect(Token::Kind kind) {
  if (cur_token->kind == kind) {
    return cur_token++;
  }
  Error(*cur_token);
}

list<Token>::iterator Expect(Token::Kind kind, const string& raw) {
  string tk_raw(cur_token->loc, cur_token->len);
  if (cur_token->kind == kind && tk_raw == raw) {
    return cur_token++;
  }
  Error(*cur_token);
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
      Token tk{Token::kOperator, p, 1, 0};
      tokens.push_back(tk);
      ++p;
      continue;
    }

    if (isdigit(*p)) {
      char* non_digit;
      long v{strtol(p, &non_digit, 0)};
      size_t len = non_digit - p;
      Token tk{Token::kInteger, p, len, v};
      tokens.push_back(tk);
      p = non_digit;
      continue;
    }

    cerr << "failed to tokenize" << endl;
    ErrorAt(p);
  }

  tokens.push_back(Token{Token::kEOF, p, 0, 0});
  return tokens;
}

void ReadAll(std::istream& is) {
  char buf[1024];
  while (auto n{is.read(buf, sizeof(buf)).gcount()}) {
    copy_n(buf, n, back_inserter(src));
  }
  src.push_back('\0');
}

int main() {
  ReadAll(cin);
  auto tokens{Tokenize(&src[0])};
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
