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

  /* num の用途
   *
   * kInt, kUInt: ビット数
   * kArray: 要素数
   */
  long num;
};

Type* NewTypeIntegral(Type::Kind kind, long bits);
Type* NewTypePointer(Type* base);
Type* NewTypeFunc(Type* ret, Type* param_list);
Type* NewTypeParam(Type* t);
Type* NewTypeUnresolved();

Type* FindType(Source& src, Token& name);
std::ostream& operator<<(std::ostream& os, Type* t);

inline std::map<std::string, Type*> builtin_types{
  {"void", new Type{Type::kVoid, nullptr, nullptr, 0}},
  {"int", NewTypeIntegral(Type::kInt, 64)},
  {"uint", NewTypeIntegral(Type::kUInt, 64)},
};
