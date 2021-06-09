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

void Scope::Enter() {
  layers_.push_back({});
  put_order_.push_back({});
}

void Scope::Leave() {
  layers_.pop_back();
  put_order_.pop_back();
}

Object* Scope::FindObject(std::string_view name) const {
  for (auto it = layers_.rbegin(); it != layers_.rend(); ++it) {
    if (auto obj_it = it->find(string{name}); obj_it != it->end()) {
      return obj_it->second;
    }
  }
  return nullptr;
}

Object* Scope::FindObjectCurrentBlock(std::string_view name) const {
  auto& bl = layers_.back();
  auto it = bl.find(string{name});
  return it == bl.end() ? nullptr : it->second;
}

void Scope::PutObject(Object* object) {
  auto& l = layers_.back();
  l.insert({string{object->id->raw}, object});
  put_order_.back().push_back(object);
}

std::vector<Object*> Scope::GetGlobals() const {
  return put_order_.front();
}
