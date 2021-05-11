#include <array>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include "magic_enum.hpp"
#include "source.hpp"
#include "token.hpp"

using namespace std;

namespace {
} // namespace

int main(int argc, char** argv) {
  Source src;
  src.ReadAll(cin);
  Tokenizer tokenizer(src);

  Token* token;
  while ((token = tokenizer.Consume()) != Token::kEOF) {
    cout << "token " << magic_enum::enum_name(token->kind) << endl;
  }
}
