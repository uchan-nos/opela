#include "typespec.hpp"

#include <execinfo.h>
#include <iostream>
#include <unistd.h>

#include "magic_enum.hpp"

using namespace std;

namespace {

[[noreturn]] void Error() {
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

Type* NewTypeArray(Type* base, long size) {
  return new Type{Type::kArray, base, nullptr, size};
}

std::ostream& operator<<(std::ostream& os, Type* t) {
  switch (t->kind) {
  case Type::kUndefined: os << "Undefined-type"; break;
  case Type::kUnresolved:
    os << "Unresolved-type(" << get<Token*>(t->value)->raw << ')';
    break;
  case Type::kInt: os << "int" << get<long>(t->value); break;
  case Type::kUInt: os << "uint" << get<long>(t->value); break;
  case Type::kPointer: os << '*' << t->base; break;
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
  case Type::kBool: os << "bool"; break;
  case Type::kArray:
    os << '[' << get<long>(t->value) << ']' << t->base;
    break;
  }
  return os;
}

size_t SizeofType(Source& src, Type* t) {
  switch (t->kind) {
  case Type::kUndefined:
  case Type::kUnresolved:
  case Type::kFunc:
    cerr << "cannot determine size: type=" << t << endl;
    Error();
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
  case Type::kBool:
    return 1;
  case Type::kArray:
    return get<long>(t->value) * SizeofType(src, t->base);
  }
  cerr << "should not come here: type=" << t << endl;
  Error();
}

Type* GetUserBaseType(Type* user_type) {
  while (user_type->kind == Type::kUser) {
    user_type = user_type->base;
  }
  return user_type;
}

bool IsEqual(Type* a, Type* b) {
  if (a == nullptr && b == nullptr) {
    return true;
  } else if (a == nullptr || b == nullptr) {
    return false;
  }

  return a->kind == b->kind && a->value == b->value &&
         IsEqual(a->base, b->base);
}

Type* TypeManager::Find(Token& name) {
  bool err;
  if (auto t = Find(string(name.raw), err); err) {
    ErrorAt(src_, name);
  } else {
    return t;
  }
}

Type* TypeManager::Find(const std::string& name) {
  bool err;
  if (auto t = Find(name, err); err) {
    Error();
  } else {
    return t;
  }
}

Type* TypeManager::Find(const std::string& name, bool& err) {
  err = false;
  if (auto it = types_.find(name); it != types_.end()) {
    return it->second;
  }

  int integral = 0;
  bool unsig = false;
  if (name.starts_with("int")) {
    integral = 3;
  } else if (name.starts_with("uint")) {
    integral = 4;
    unsig = true;
  }
  if (0 < integral && integral < name.length()) {
    int bits = 0;
    for (int i = integral; i < name.length(); ++i) {
      if (!isdigit(name[i])) {
        cerr << "bit width must be a 10-base number" << endl;
        err = true;
        return nullptr;
      }
      bits = 10*bits + name[i] - '0';
    }
    auto t = NewTypeIntegral(unsig ? Type::kUInt : Type::kInt, bits);
    types_[name] = t;
    return t;
  }

  return nullptr;
}

Type* TypeManager::Register(Type* t) {
  string name(get<Token*>(t->value)->raw);
  auto old_t = types_[name];
  types_[name] = t;
  return old_t;
}
