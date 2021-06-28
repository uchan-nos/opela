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
