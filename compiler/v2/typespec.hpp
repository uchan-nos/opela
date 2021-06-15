#pragma once

#include <map>
#include <string>

#include "source.hpp"
#include "token.hpp"

// 抽象構文木での型表現
struct Type {
  enum Kind {
    kUndefined,  // そもそも型という概念がない文法要素の場合
    kUnresolved, // 識別子に出会った時点で宣言が見つかっていない型
    kInt,
    kUInt,
    kPointer,
    kFunc,
    kParam,
    kVParam, // 可変長仮引数
    kVoid,
    kUser, // ユーザー定義型
    kBool,
    kArray,
    kInitList,
    kStruct,
  } kind;

  Type* base;
  Type* next;

  /* base と next の用途
   *
   * kPointer
   *   base: ポインタのベース型
   * kFunc
   *   base: 戻り値型
   *   next: 引数リスト
   * kParam
   *   base: 引数型
   *   next: 次の引数
   * kArray:
   *   base: 要素の型
   * kInitList:
   *   next: 要素リスト（kParam のリスト）
   * kStruct:
   *   next: フィールドリスト（kParam のリスト）
   */

  /* value の用途
   *
   * kInt, kUInt: [long] ビット数
   * kArray:      [long] 要素数
   * kParam:      [Token*] 引数名、フィールド名（無名なら nullptr）
   * kUnresolved: [Token*] 解決されていない型の名前
   * kUser:       [Token*] 型名
   */
  std::variant<long, Token*> value;
};

Type* NewType(Type::Kind kind);
Type* NewTypeIntegral(Type::Kind kind, long bits);
Type* NewTypePointer(Type* base);
Type* NewTypeFunc(Type* ret, Type* param_list);
Type* NewTypeParam(Type* t, Token* name);
Type* NewTypeUnresolved(Token* name);
Type* NewTypeUser(Type* base, Token* name);
Type* NewTypeArray(Type* base, long size);

std::ostream& operator<<(std::ostream& os, Type* t);
size_t SizeofType(Source& src, Type* t);
Type* GetUserBaseType(Type* user_type);

inline bool IsIntegral(Type* t) {
  return t->kind == Type::kInt || t->kind == Type::kUInt;
}
bool IsEqual(Type* a, Type* b);

class TypeManager {
 public:
  TypeManager(Source& src) : src_{src} {}

  Type* Find(Token& name);
  Type* Find(const std::string& name);
  Type* Register(Type* t); // 同じ名前でこれまで登録されていた型を返す

 private:
  Type* Find(const std::string& name, bool& err);

  Source& src_;
  std::map<std::string, Type*> types_{
    {"void", new Type{Type::kVoid, nullptr, nullptr, 0}},
    {"int", NewTypeIntegral(Type::kInt, 64)},
    {"uint", NewTypeIntegral(Type::kUInt, 64)},
    {"bool", new Type{Type::kBool, nullptr, nullptr, 0}},
    {"byte", NewTypeIntegral(Type::kUInt, 8)},
  };
};
