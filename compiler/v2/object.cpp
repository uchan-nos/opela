#include "object.hpp"

#include <iostream>

using namespace std;

std::ostream& operator<<(std::ostream& os, Object* o) {
  char linkage = '?';
  switch (o->linkage) {
  case Object::kLocal: linkage = 'L'; break;
  case Object::kGlobal: linkage = 'G'; break;
  case Object::kExternal: linkage = 'E'; break;
  }

  switch (o->kind) {
  case Object::kUnresolved:
    os << "Unresolved[" << o->id->raw << ']';
    break;
  case Object::kVar:
    os << linkage << "Var[" << o->id->raw << ' ';
    if (o->type) {
      os << o->type << ']';
    } else {
      os << "type=null]";
    }
    break;
  case Object::kFunc:
    os << linkage << "Func[" << o->id->raw << ' ';
    if (o->type) {
      os << o->type << ']';
    } else {
      os << "type=null]";
    }
    break;
  }

  return os;
}
