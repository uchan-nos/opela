#include "typespec.hpp"

#include <execinfo.h>
#include <iostream>
#include <unistd.h>

#include "generics.hpp"
#include "magic_enum.hpp"

using namespace std;

namespace {

[[noreturn]] void Error() {
  array<void*, 128> trace{};
  int n = backtrace(trace.begin(), trace.size());
  backtrace_symbols_fd(trace.begin(), n, STDERR_FILENO);

  exit(1);
}

}

Type* NewType(Type::Kind kind) {
  return new Type{kind, nullptr, nullptr, 0};
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

Type* NewTypeGParam(Token* name) {
  return new Type{Type::kGParam, nullptr, nullptr, name};
}

Type* NewTypeGeneric(Type* gtype, Type* param_list) {
  return new Type{Type::kGeneric, gtype, param_list, 0};
}

std::ostream& PrintType(std::ostream& os, Type* t, int depth) {
  if (depth == 4) {
    os << "~";
    return os;
  }

  switch (t->kind) {
  case Type::kUndefined: os << "Undefined-type"; break;
  case Type::kUnresolved:
    os << "Unresolved-type(" << get<Token*>(t->value)->raw << ')';
    break;
  case Type::kInt: os << "int" << get<long>(t->value); break;
  case Type::kUInt: os << "uint" << get<long>(t->value); break;
  case Type::kPointer: PrintType(os << '*', t->base, depth + 1); break;
  case Type::kFunc:
    os << "func(";
    if (auto pt = t->next) {
      PrintType(os, pt, depth + 1);
      for (pt = pt->next; pt; pt = pt->next) {
        PrintType(os << ',', pt, depth + 1);
      }
    }
    PrintType(os << ')', t->base, depth + 1);
    break;
  case Type::kParam:
    if (auto name = get<Token*>(t->value)) {
      os << name->raw << ' ';
    }
    PrintType(os, t->base, depth + 1);
    break;
  case Type::kVParam:
    os << "...";
    break;
  case Type::kVoid: os << "void"; break;
  case Type::kUser:
    os << get<Token*>(t->value)->raw;
    break;
  case Type::kBool: os << "bool"; break;
  case Type::kArray:
    PrintType(os << '[' << get<long>(t->value) << ']', t->base, depth + 1);
    break;
  case Type::kInitList:
    os << '{';
    if (auto pt = t->next) {
      PrintType(os, pt->base, depth + 1);
      for (pt = pt->next; pt; pt = pt->next) {
        PrintType(os << ',', pt->base, depth + 1);
      }
    }
    os << '}';
    break;
  case Type::kStruct:
    os << "struct{";
    if (auto ft = t->next) {
      PrintType(os, ft, depth + 1);
      for (ft = ft->next; ft; ft = ft->next) {
        PrintType(os << ',', ft, depth + 1);
      }
    }
    os << '}';
    break;
  case Type::kGParam:
    os << get<Token*>(t->value)->raw;
    break;
  case Type::kGeneric:
    PrintType(os, t->base, depth + 1) << '<';
    if (auto gparam = t->next) {
      os << get<Token*>(gparam->value)->raw;
      for (gparam = gparam->next; gparam; gparam = gparam->next) {
        os << ',' << get<Token*>(gparam->value)->raw;
      }
    }
    os << '>';
    break;
  case Type::kConcrete:
    PrintType(os, t->base, depth + 1) << '<';
    if (auto param = t->next) {
      os << param->base;
      for (param = param->next; param; param = param->next) {
        PrintType(os << ',', param->base, depth + 1);
      }
    }
    os << '>';
    break;
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, Type* t) {
  return PrintType(os, t, 0);
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
  case Type::kVParam:
    cerr << "sizeof kVParam is not defined" << endl;
    Error();
  case Type::kVoid:
    return 0;
  case Type::kUser:
    return SizeofType(src, t->base);
  case Type::kBool:
    return 1;
  case Type::kArray:
    return get<long>(t->value) * SizeofType(src, t->base);
  case Type::kInitList:
    cerr << "sizeof kInitList is not defined" << endl;
    Error();
  case Type::kStruct:
    {
      size_t s = 0;
      for (auto field_t = t->next; field_t; field_t = field_t->next) {
        s += SizeofType(src, field_t);
      }
      return s;
    }
  case Type::kGParam:
    cerr << "sizeof kGParam is not defined" << endl;
    Error();
  case Type::kGeneric:
    cerr << "sizeof kGeneric is not defined" << endl;
    Error();
  case Type::kConcrete:
    return SizeofType(src, ConcretizeType(t));
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

Type* GetPrimaryType(Type* type) {
  while (type->kind == Type::kUser || type->kind == Type::kGeneric ||
         type->kind == Type::kConcrete) {
    type = type->base;
  }
  return type;
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

TypeManager::TypeManager(Source& src) : src_{src} {
  types_.Put("void", new Type{Type::kVoid, nullptr, nullptr, 0});
  types_.Put("int", NewTypeIntegral(Type::kInt, 64));
  types_.Put("uint", NewTypeIntegral(Type::kUInt, 64));
  types_.Put("bool", new Type{Type::kBool, nullptr, nullptr, 0});
  types_.Put("byte", NewTypeIntegral(Type::kUInt, 8));
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
  if (auto t = types_.Find(name)) {
    return t;
  }

  size_t integral = 0;
  bool unsig = false;
  if (name.starts_with("int")) {
    integral = 3;
  } else if (name.starts_with("uint")) {
    integral = 4;
    unsig = true;
  }
  if (0 < integral && integral < name.length()) {
    int bits = 0;
    for (size_t i = integral; i < name.length(); ++i) {
      if (!isdigit(name[i])) {
        cerr << "bit width must be a 10-base number" << endl;
        err = true;
        return nullptr;
      }
      bits = 10*bits + name[i] - '0';
    }
    auto t = NewTypeIntegral(unsig ? Type::kUInt : Type::kInt, bits);
    types_.Put(name, t);
    return t;
  }

  return nullptr;
}

Type* TypeManager::Register(Type* t) {
  string name(get<Token*>(t->value)->raw);
  return types_.Put(name, t);
}
