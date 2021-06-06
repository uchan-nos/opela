#include "typespec.hpp"

#include <execinfo.h>
#include <iostream>
#include <unistd.h>

#include "magic_enum.hpp"

using namespace std;

namespace {

[[noreturn]] void Error(const char* msg, Type* t) {
  cerr << msg << ": type=" << t << endl;

  array<void*, 128> trace;
  int n = backtrace(trace.begin(), trace.size());
  backtrace_symbols_fd(trace.begin(), n, STDERR_FILENO);

  exit(1);
}

}

Type* NewTypeIntegral(Type::Kind kind, long bits) {
  return new Type{kind, nullptr, nullptr, bits};
}

Type* NewTypePointer(Type* base) {
  return new Type{Type::kPointer, base, nullptr, 0};
}

Type* NewTypeFunc(Type* ret, Type* param_list) {
  return new Type{Type::kFunc, ret, param_list, 0};
}

Type* NewTypeParam(Type* t, Token* name) {
  return new Type{Type::kParam, t, nullptr, name};
}

Type* NewTypeUnresolved(Token* name) {
  return new Type{Type::kUnresolved, nullptr, nullptr, name};
}

Type* NewTypeUser(Type* base, Token* name) {
  return new Type{Type::kUser, base, nullptr, name};
}

Type* FindType(Source& src, Token& name) {
  string tname(name.raw);
  if (auto it = builtin_types.find(tname); it != builtin_types.end()) {
    return it->second;
  }

  int integral = 0;
  bool unsig = false;
  if (name.raw.starts_with("int")) {
    integral = 3;
  } else if (name.raw.starts_with("uint")) {
    integral = 4;
    unsig = true;
  }
  if (0 < integral && integral < name.raw.length()) {
    int bits = 0;
    for (int i = integral; i < name.raw.length(); ++i) {
      if (!isdigit(name.raw[i])) {
        cerr << "bit width must be a 10-base number" << endl;
        ErrorAt(src, &name.raw[i]);
      }
      bits = 10*bits + name.raw[i] - '0';
    }
    auto t = NewTypeIntegral(unsig ? Type::kUInt : Type::kInt, bits);
    builtin_types[tname] = t;
    return t;
  }

  return nullptr;
}

std::ostream& operator<<(std::ostream& os, Type* t) {
  switch (t->kind) {
  case Type::kUndefined: os << "Undefined-type"; break;
  case Type::kUnresolved:
    os << "Unresolved-type(" << get<Token*>(t->value)->raw << ')';
    break;
  case Type::kInt: os << "int" << get<long>(t->value); break;
  case Type::kUInt: os << "uint" << get<long>(t->value); break;
  case Type::kPointer: os << '*' << get<long>(t->value); break;
  case Type::kFunc:
    os << "func(";
    if (auto pt = t->next) {
      os << pt;
      for (pt = pt->next; pt; pt = pt->next) {
        os << ',' << pt;
      }
    }
    os << ')' << t->base;
    break;
  case Type::kParam:
    if (auto name = get<Token*>(t->value)) {
      os << name->raw << ' ';
    }
    os << t->base;
    break;
  case Type::kVoid: os << "void"; break;
  case Type::kUser:
    os << get<Token*>(t->value)->raw;
    break;
  }
  return os;
}

size_t SizeofType(Source& src, Type* t) {
  switch (t->kind) {
  case Type::kUndefined:
  case Type::kUnresolved:
  case Type::kFunc:
    Error("cannot determine size", t);
  case Type::kInt:
  case Type::kUInt:
    return (get<long>(t->value) + 7) / 8;
  case Type::kPointer:
    return 8;
  case Type::kParam:
    return SizeofType(src, t->base);
  case Type::kVoid:
    return 0;
  case Type::kUser:
    return SizeofType(src, t->base);
  }
  Error("should not come here", t);
}
