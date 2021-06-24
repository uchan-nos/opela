#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "token.hpp"

template <class T>
class Scope {
 public:
  void Enter();
  void Leave();

  T* Find(const std::string& name) const;
  T* Find(Token& name) const {
    return Find(std::string{name.raw});
  }

  T* FindCurrentBlock(const std::string& name) const;
  T* FindCurrentBlock(Token& name) const {
    return FindCurrentBlock(std::string{name.raw});
  }

  /* 名前表に登録する。
   *
   * 既に同じ名前で登録されていた場合は上書きをせず、
   * 既に登録されているオブジェクトを返す。
   */
  T* Put(const std::string& name, T* v);
  T* Put(Token& name, T* v) {
    return Put(std::string{name.raw}, v);
  }

  std::vector<T*> GetGlobals() const;

 private:
  // 名前表（先頭がグローバル、末尾が現在のブロック）
  std::vector<std::map<std::string, T*>> layers_{{}};

  // 登録順を記憶する配列
  std::vector<std::vector<T*>> put_order_{{}};
};

template <class T>
void Scope<T>::Enter() {
  layers_.push_back({});
  put_order_.push_back({});
}

template <class T>
void Scope<T>::Leave() {
  layers_.pop_back();
  put_order_.pop_back();
}

template <class T>
T* Scope<T>::Find(const std::string& name) const {
  for (auto lay_it = layers_.rbegin(); lay_it != layers_.rend(); ++lay_it) {
    if (auto it = lay_it->find(name); it != lay_it->end()) {
      return it->second;
    }
  }
  return nullptr;
}

template <class T>
T* Scope<T>::FindCurrentBlock(const std::string& name) const {
  auto& bl = layers_.back();
  auto it = bl.find(name);
  return it == bl.end() ? nullptr : it->second;
}

template <class T>
T* Scope<T>::Put(const std::string& name, T* v) {
  auto& l = layers_.back();
  auto [ old_it, inserted ] = l.insert({name, v});
  if (inserted) {
    put_order_.back().push_back(v);
    return nullptr;
  }
  return old_it->second;
}

template <class T>
std::vector<T*> Scope<T>::GetGlobals() const {
  return put_order_.front();
}
