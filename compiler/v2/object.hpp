#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "token.hpp"

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
  bool is_local; // グローバルなら false

  int bp_offset; // ローカル変数の BP からのオフセット

  std::vector<Object*> locals; // 関数のローカル変数リスト
};

inline Object* NewLVar(Token* id) {
  return new Object{Object::kVar, id, true, -1, {}};
}

inline Object* NewFunc(Token* id) {
  return new Object{Object::kFunc, id, false, -1, {}};
}

class Scope {
 public:
  void Enter();
  void Leave();

  Object* FindObject(std::string_view name);
  Object* FindObjectCurrentBlock(std::string_view name);
  void PutObject(Object* obj);

 private:
  // 名前表（先頭がグローバル、末尾が現在のブロック）
  std::vector<std::map<std::string, Object*>> layers_;
};
