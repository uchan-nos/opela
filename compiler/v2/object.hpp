#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "token.hpp"
#include "typespec.hpp"

struct Node;

// アドレスを持つもの（列挙子や型名は含まない）
struct Object {
  enum Kind {
    // 識別子に出会った時点で宣言が見つかっていない状態
    // （型名や列挙子かもしれない）
    kUnresolved,

    kVar,  // 変数
    kFunc, // 関数
  } kind;

  Token* id; // オブジェクト名（マングルされた名前ではない）
  Node* def; // オブジェクトを定義するノード（kDefVar, kDefFunc, kExtern）
  Type* type;

  enum Linkage {
    kLocal,
    kGlobal,
    kExternal,
  } linkage;

  int bp_offset; // ローカル変数の BP からのオフセット

  std::vector<Object*> locals; // 関数のローカル変数リスト
};

inline Object* NewVar(Token* id, Node* def, Object::Linkage linkage) {
  return new Object{Object::kVar, id, def, nullptr, linkage, -1, {}};
}

inline Object* NewFunc(Token* id, Node* def, Object::Linkage linkage) {
  return new Object{Object::kFunc, id, def, nullptr, linkage, -1, {}};
}

std::ostream& operator<<(std::ostream& os, Object* o);
