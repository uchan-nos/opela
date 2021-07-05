#include "generics.hpp"

#include <cassert>
#include <cstring>
#include <functional>
#include <iostream>
#include <sstream>

#include "ast.hpp"
#include "magic_enum.hpp"

using namespace std;

struct ConcContext {
  Source& src;
  TypeMap& gtype;
  Object* func; // 具体化を実行中の関数オブジェクト
};

namespace {

std::string Mangle(Type* t) {
  ostringstream oss;
  switch (t->kind) {
  case Type::kInt:
    oss << "int" << get<long>(t->value);
    break;
  case Type::kUInt:
    oss << "uint" << get<long>(t->value);
    break;
  case Type::kPointer:
    oss << "ptr_" << Mangle(t->base);
    break;
  default:
    cerr << "Mangle logic is not implemented for " << t << endl;
  }
  return oss.str();
}

std::string Mangle(std::string_view base_name, Type* t) {
  if (t->kind == Type::kFunc) {
    ostringstream oss;
    oss << base_name;
    for (auto pt = t->next; pt; pt = pt->next) {
      oss << "__" << Mangle(pt->base);
    }
    return oss.str();
  } else {
    cerr << "Mangle logic is not implemented for " << t << endl;
    assert(false);
  }
}

Node* ConcretizeNode(ConcContext& ctx, Node* node) {
  if (node == nullptr) {
    return nullptr;
  }

  Node* dup = nullptr;
  switch (node->kind) {
  case Node::kParam:
    dup = NewNode(Node::kParam, node->token);
    dup->lhs = ConcretizeNode(ctx, node->lhs);
    dup->next = ConcretizeNode(ctx, node->next);
    dup->type = dup->lhs->type;
    break;
  case Node::kType:
    dup = NewNodeType(node->token, ConcretizeType(ctx.gtype, node->type));
    break;
  case Node::kBlock:
    dup = NewNode(Node::kBlock, node->token);
    for (auto stmt = node->next, cur = dup; stmt;
         stmt = stmt->next, cur = cur->next) {
      cur->next = ConcretizeNode(ctx, stmt);
    }
    break;
  case Node::kRet:
    dup = NewNode(Node::kRet, node->token);
    dup->lhs = ConcretizeNode(ctx, node->lhs);
    dup->type = dup->lhs->type;
    break;
  case Node::kAdd:
    dup = NewNode(Node::kAdd, node->token);
    dup->lhs = ConcretizeNode(ctx, node->lhs);
    dup->rhs = ConcretizeNode(ctx, node->rhs);
    dup->type = MergeTypeBinOp(dup->lhs->type, dup->rhs->type);
    break;
  case Node::kId:
    dup = NewNode(Node::kId, node->token);
    if (auto p = get_if<Object*>(&node->value)) {
      Object* obj = *p;
      auto obj_dup = new Object{*obj};
      for (size_t i = 0; i < ctx.func->locals.size(); ++i) {
        if (ctx.func->locals[i] == obj) {
          ctx.func->locals[i] = obj_dup;
          break;
        }
      }
      dup->value = obj_dup;
      dup->type = obj_dup->type = ConcretizeType(ctx.gtype, obj->type);
    } else {
      dup->type = ConcretizeType(ctx.gtype, node->type);
    }
    break;
  case Node::kArrow:
    dup = NewNode(Node::kArrow, node->token);
    dup->lhs = ConcretizeNode(ctx, node->lhs);
    dup->rhs = ConcretizeNode(ctx, node->rhs);
    if (auto p = GetUserBaseType(dup->lhs->type); p->kind != Type::kPointer) {
      cerr << "lhs must be a pointer to a struct: " << p << endl;
      ErrorAt(ctx.src, *node->token);
    } else if (auto t = GetUserBaseType(p->base); t->kind != Type::kStruct) {
      cerr << "lhs must be a pointer to a struct: " << t << endl;
      ErrorAt(ctx.src, *node->token);
    } else {
      for (auto ft = t->next; ft; ft = ft->next) {
        if (get<Token*>(ft->value)->raw == node->rhs->token->raw) {
          dup->type = ft->base;
          break;
        }
      }
      if (dup->type == nullptr) {
        cerr << "no such member" << endl;
        ErrorAt(ctx.src, *node->rhs->token);
      }
    }
    break;
  default:
    cerr << "ConcretizeNode: not implemented" << endl;
    ErrorAt(ctx.src, *node->token);
  }

  return dup;
}

} // namespace

Type* ConcretizeType(TypeMap& gtype, Type* type) {
  if (type == nullptr) {
    return nullptr;
  }
  if (type->base == nullptr && type->next == nullptr) {
    if (type->kind != Type::kGParam) {
      return type;
    }
    return gtype[string(get<Token*>(type->value)->raw)];
  }

  auto base_dup = ConcretizeType(gtype, type->base);
  auto next_dup = ConcretizeType(gtype, type->next);
  if (base_dup == type->base && next_dup == type->next) {
    return type;
  }

  auto dup = NewType(type->kind);
  dup->base = base_dup;
  dup->next = next_dup;
  dup->value = type->value;
  return dup;
}

Type* ConcretizeType(Type* type) {
  if (type == nullptr) {
    return nullptr;
  } else if (type->kind != Type::kConcrete) {
    return type;
  }

  auto generic_t = GetUserBaseType(type->base);
  auto type_list = type->next;

  TypeMap gtype;
  auto gparam = generic_t->next;
  for (auto param = type_list; param; param = param->next) {
    string gname{get<Token*>(gparam->value)->raw};
    gtype[gname] = param->base;
    gparam = gparam->next;
  }

  auto conc_t = ConcretizeType(gtype, generic_t->base);
  return conc_t;
}

TypedFunc* NewTypedFunc(ASTContext& ctx, Object* gfunc, Node* type_list) {
  assert(type_list->kind == Node::kTList);

  auto cf = new TypedFunc{{}, gfunc};
  auto gname = gfunc->def->rhs; // generic name list: T, S, ...
  for (auto tname = type_list->lhs; tname; tname = tname->next) {
    if (auto t = ctx.tm.Find(*tname->token)) {
      cf->gtype[string(gname->token->raw)] = t;
    } else {
      cerr << "unknown type name" << endl;
      ErrorAt(ctx.src, *tname->token);
    }
    gname = gname->next;
  }

  return cf;
}

Type* ConcretizeType(TypedFunc& f) {
  return ConcretizeType(f.gtype, f.func->type);
}

std::string Mangle(TypedFunc& f) {
  return Mangle(f.func->id->raw, ConcretizeType(f));
}

Node* ConcretizeDefFunc(Source& src, TypeMap& gtype, Node* def) {
  assert(def->kind == Node::kDefFunc);
  auto def_dup = NewNode(Node::kDefFunc, def->token);

  auto func = get<Object*>(def->value);
  Type* conc_func_t = ConcretizeType(gtype, func->type);
  char* conc_name = strdup(Mangle(func->id->raw, conc_func_t).c_str());
  auto conc_name_token = new Token{Token::kId, string_view{conc_name}, {}};

  auto obj_dup = NewFunc(conc_name_token, def, func->linkage);
  obj_dup->locals = func->locals;
  obj_dup->type = conc_func_t;
  ConcContext ctx{src, gtype, obj_dup};

  def_dup->lhs = ConcretizeNode(ctx, def->lhs);
  def_dup->rhs = ConcretizeNode(ctx, def->rhs);
  def_dup->cond = ConcretizeNode(ctx, def->cond);

  def_dup->value = obj_dup;

  return def_dup;
}
