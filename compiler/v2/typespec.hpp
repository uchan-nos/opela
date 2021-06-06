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
    kVoid,
    kUser, // ユーザー定義型
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

Type* NewTypeIntegral(Type::Kind kind, long bits);
Type* NewTypePointer(Type* base);
Type* NewTypeFunc(Type* ret, Type* param_list);
Type* NewTypeParam(Type* t, Token* name);
Type* NewTypeUnresolved(Token* name);
Type* NewTypeUser(Type* base, Token* name);

Type* FindType(Source& src, Token& name);
std::ostream& operator<<(std::ostream& os, Type* t);
size_t SizeofType(Source& src, Type* t);

inline std::map<std::string, Type*> builtin_types{
  {"void", new Type{Type::kVoid, nullptr, nullptr, 0}},
  {"int", NewTypeIntegral(Type::kInt, 64)},
  {"uint", NewTypeIntegral(Type::kUInt, 64)},
};
