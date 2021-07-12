#pragma once

#include <map>
#include <string>

#include "object.hpp"
#include "source.hpp"
#include "typespec.hpp"

using TypeMap = std::map<std::string, Type*>;
Type* ConcretizeType(TypeMap& gtype, Type* type);
Type* ConcretizeType(Type* type);

struct TypedFunc {
  TypeMap gtype; // "T": t1, "S": t2, ...
  Object* func; // ジェネリック関数、あるいは普通の関数
};

struct ASTContext;
struct Node;

TypedFunc* NewTypedFunc(ASTContext& ctx, Object* gfunc, Node* type_list);
Type* ConcretizeType(TypedFunc& f);
std::string Mangle(TypedFunc& f);

// 与えられた kDefFunc ノードを複製しつつ型を具体化する
// 戻り値は型が具体化されたノード
Node* ConcretizeDefFunc(Source& src, TypeMap& gtype, Node* def);
