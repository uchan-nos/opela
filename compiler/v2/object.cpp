#include "object.hpp"

#include <iostream>

using namespace std;

void Scope::Enter() {
  layers_.push_back({});
}

void Scope::Leave() {
  layers_.pop_back();
}

Object* Scope::FindObject(std::string_view name) {
  for (auto it = layers_.rbegin(); it != layers_.rend(); ++it) {
    if (auto obj_it = it->find(string{name}); obj_it != it->end()) {
      return obj_it->second;
    }
  }
  return nullptr;
}

Object* Scope::FindObjectCurrentBlock(std::string_view name) {
  auto& bl = layers_.back();
  auto it = bl.find(string{name});
  return it == bl.end() ? nullptr : it->second;
}

void Scope::PutObject(Object* object) {
  auto& l = layers_.back();
  l.insert({string{object->id->raw}, object});
}
