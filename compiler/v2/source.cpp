#include "source.hpp"

#include <array>
#include <cstdlib>
#include <execinfo.h>
#include <iostream>
#include <string_view>
#include <unistd.h>

using namespace std;

void Source::ReadAll(istream& is) {
  array<char, 1024> buf;
  while (auto n = is.read(buf.begin(), buf.size()).gcount()) {
    src_.append(buf.begin(), n);
  }
  src_.append(1, '\0');
}

std::string_view Source::GetLine(const char* loc) {
  auto line = loc;
  while (&*src_.begin() < line && line[-1] != '\n') {
    --line;
  }
  auto line_end = loc;
  while (*line_end != '\n' && line_end < &*src_.end()) {
    ++line_end;
  }
  return {line, line_end};
}

void ErrorAt(Source& src, const char* loc) {
  auto line = src.GetLine(loc);

  cerr << line << endl;
  cerr << string(&*loc - line.begin(), ' ') << '^' << endl;

  array<void*, 128> trace{};
  int n = backtrace(trace.begin(), trace.size());
  backtrace_symbols_fd(trace.begin(), n, STDERR_FILENO);

  exit(1);
}
