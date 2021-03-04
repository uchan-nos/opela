#include "token.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <string_view>

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
  {Token::kVar,    "var"},
  {Token::kExtern, "extern"},
  {Token::kSizeof, "sizeof"},
  {Token::kBreak,  "break"},
  {Token::kCont,   "continue"},
  {Token::kType,   "type"},
  {Token::kStruct, "struct"},
};

bool IsAlpha(char ch) {
  return isalpha(ch) || ch == '_';
}

bool IsAlNum(char ch) {
  return isalnum(ch) || ch == '_';
}

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

} // namespace

vector<Token> Tokenize(const char* p) {
  vector<Token> tokens;

  while (*p) {
    if (isspace(*p)) {
      ++p;
      continue;
    }

    if (p[0] == '/') {
      if (p[1] == '/') {
        if (auto lf{strchr(p + 2, '\n')}) {
          p = lf + 1;
          continue;
        }
        break;
      } else if (p[1] == '*') {
        if (auto delim{strstr(p + 2, "*/")}) {
          p = delim + 2;
          continue;
        }
        break;
      }
    }

    if (isdigit(*p)) {
      if (p[1] == 'b') {
        // binary literal
        long v{0};
        size_t i = 2;
        for (; p[i] == '0' || p[i] == '1'; ++i) {
          v = (v << 1) + p[i] - '0';
        }
        Token tk{Token::kInt, p, i, v};
        tokens.push_back(tk);
        p += i;
        continue;
      }

      char* non_digit;
      long v{strtol(p, &non_digit, 0)};
      size_t len = non_digit - p;
      Token tk{Token::kInt, p, len, v};
      tokens.push_back(tk);
      p = non_digit;
      continue;
    }

    if (string_view op{p, 3}; op == "...") {
      tokens.push_back(Token{Token::kReserved, p, 3, 0});
      p += 3;
      continue;
    }

    if (p[1] == '=' && strchr("=!<>:+-*/", p[0])) {
      Token tk{Token::kReserved, p, 2, 0};
      tokens.push_back(tk);
      p += 2;
      continue;
    }

    if (string_view op{p, 2};
        op == "||" || op == "&&" || op == "++" || op == "--" || op == "->") {
      tokens.push_back(Token{Token::kReserved, p, 2, 0});
      p += 2;
      continue;
    }

    if (strchr("+-*/()<>;{}=,&[].@", *p)) {
      Token tk{Token::kReserved, p, 1, 0};
      tokens.push_back(tk);
      ++p;
      continue;
    }

    if (p[0] == '\'') {
      if (p[1] != '\\' && p[2] == '\'') {
        tokens.push_back(Token{Token::kChar, p, 3, p[1]});
        p += 3;
      } else if (p[1] == '\\' && p[3] == '\'') {
        tokens.push_back(Token{Token::kChar, p, 4, GetEscapeValue(p[2])});
        p += 4;
      }
      continue;
    }

    if (*p == '"') {
      auto str_end{FindStr(p)};
      Token tk{Token::kStr, p, static_cast<size_t>(str_end - p), 0};
      tokens.push_back(tk);
      p = str_end;
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

Token* Peek(Token::Kind kind) {
  if (cur_token->kind == kind) {
    return &*cur_token;
  }
  return nullptr;
}

Token* Peek(const string& raw) {
  if (cur_token->kind == Token::kReserved && cur_token->Raw() == raw) {
    return &*cur_token;
  }
  return nullptr;
}

Token* Consume(Token::Kind kind) {
  auto tk{Peek(kind)};
  if (tk) {
    ++cur_token;
  }
  return tk;
}

Token* Consume(const string& raw) {
  auto tk{Peek(raw)};
  if (tk) {
    ++cur_token;
  }
  return tk;
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
