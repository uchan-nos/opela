#include "mangle.hpp"

#include <cassert>
#include <iostream>
#include <sstream>

#include "typespec.hpp"

using namespace std;

std::string Mangle(Type* t) {
  ostringstream oss;
  switch (t->kind) {
  case Type::kInt:
    oss << "int" << get<long>(t->value);
    break;
  case Type::kUInt:
    oss << "uint" << get<long>(t->value);
    break;
  case Type::kPointer:
    oss << "ptr_" << Mangle(t->base);
    break;
  case Type::kUser:
    oss << get<Token*>(t->value)->raw;
    break;
  case Type::kStruct:
    oss << "struct";
    for (auto param = t->next; param; param = param->next) {
      oss << '_' << Mangle(param->base);
    }
    break;
  case Type::kGParam:
    oss << get<Token*>(t->value)->raw;
    break;
  case Type::kConcrete:
    oss << Mangle(t->base);
    for (auto param = t->next; param; param = param->next) {
      oss << '_' << Mangle(param->base);
    }
    break;
  default:
    cerr << "Mangle logic is not implemented for " << t << endl;
  }
  return oss.str();
}

std::string Mangle(std::string_view base_name, Type* t) {
  if (t->kind == Type::kFunc) {
    ostringstream oss;
    oss << base_name;
    for (auto pt = t->next; pt && pt->kind != Type::kVParam; pt = pt->next) {
      oss << "__" << Mangle(pt->base);
    }
    return oss.str();
  } else {
    cerr << "Mangle logic is not implemented for " << t << endl;
    assert(false);
  }
}
