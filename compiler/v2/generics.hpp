#pragma once

#include <map>
#include <string>

#include "object.hpp"
#include "source.hpp"
#include "typespec.hpp"

struct ConcreteFunc {
  Object* func; // ジェネリック関数または普通の関数
  std::map<std::string, Type*> gtype; // "T": t1, "S": t2, ...
};

struct ASTContext;
struct Node;

std::string Mangle(ConcreteFunc& f);
std::string Mangle(Source& src, ConcreteFunc& f);
Type* CalcConcreteType(ConcreteFunc& f);
Type* CalcConcreteType(Source& src, ConcreteFunc& f);
ConcreteFunc* ConcretizeFunc(ASTContext& ctx, Object* gfunc, Node* type_list);

struct ConcContext {
  Source& src;
  std::map<std::string, Type*>& gtype;
  Object* func; // 具体化を実行中の関数オブジェクト
};

// 与えられた kDefFunc ノードを複製しつつ型を具体化する
// 戻り値は型が具体化されたノード
Node* ConcretizeDefFunc(ConcContext& ctx, Node* def);
