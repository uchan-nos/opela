#include "source.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <execinfo.h>

using namespace std;

void ErrorAt(const char* loc) {
  auto line{loc};
  while (&src[0] < line && line[-1] != '\n') {
    --line;
  }
  auto line_end{loc};
  while (*line_end != '\n' && *line_end != '\0') {
    ++line_end;
  }

  cerr << string(line, line_end - line) << endl;
  cerr << string(loc - line, ' ') << '^' << endl;

  array<void*, 128> trace;
  int n{backtrace(&trace[0], trace.size())};
  backtrace_symbols_fd(&trace[0], n, 2);

  exit(1);
}

void ReadAll(istream& is) {
  char buf[1024];
  while (auto n{is.read(buf, sizeof(buf)).gcount()}) {
    copy_n(buf, n, back_inserter(src));
  }
  src.push_back('\0');
}
