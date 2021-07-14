#include "generics.hpp"

#include <cassert>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>

#include "ast.hpp"
#include "magic_enum.hpp"

using namespace std;

struct ConcContext {
  Source& src;
  TypeMap& gtype;
  Object* func; // 具体化を実行中の関数オブジェクト
  std::map<Object*, Object*>& new_lvars;
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
  case Type::kUser:
    oss << get<Token*>(t->value)->raw;
    break;
  case Type::kStruct:
    oss << "struct";
    for (auto param = t->next; param; param = param->next) {
      oss << '_' << Mangle(param->base);
    }
    break;
  case Type::kGParam:
    oss << get<Token*>(t->value)->raw;
    break;
  case Type::kConcrete:
    oss << Mangle(t->base);
    for (auto param = t->next; param; param = param->next) {
      oss << '_' << Mangle(param->base);
    }
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

  if (node->kind == Node::kType) {
    return NewNodeType(node->token, ConcretizeType(ctx.gtype, node->type));
  } else if (node->kind == Node::kId) {
    auto dup = NewNode(Node::kId, node->token);
    if (auto p = get_if<Object*>(&node->value)) {
      Object* obj = *p;
      if (auto it = ctx.new_lvars.find(obj); it != ctx.new_lvars.end()) {
        dup->value = it->second;
        dup->type = it->second->type;
      } else {
        auto obj_dup = new Object{*obj};
        dup->value = obj_dup;
        dup->type = obj_dup->type = ConcretizeType(ctx.gtype, obj->type);
      }
    } else {
      dup->type = ConcretizeType(ctx.gtype, node->type);
    }
    return dup;
  }

  auto lhs = ConcretizeNode(ctx, node->lhs);
  auto rhs = ConcretizeNode(ctx, node->rhs);
  auto cond = ConcretizeNode(ctx, node->cond);
  auto next = ConcretizeNode(ctx, node->next);
  if (lhs == node->lhs && rhs == node->rhs &&
      cond == node->cond && next == node->next) {
    return node;
  }

  auto dup = NewNode(node->kind, node->token);
  dup->lhs = lhs;
  dup->rhs = rhs;
  dup->cond = cond;
  dup->next = next;
  dup->value = node->value;
  if (auto p = get_if<Object*>(&node->value)) {
    Object* obj = *p;
    get<Object*>(dup->value)->type = ConcretizeType(ctx.gtype, obj->type);
  } else if (auto p = get_if<TypedFunc*>(&node->value)) {
    TypedFunc* tf = *p;
    get<TypedFunc*>(dup->value)->func->type =
      ConcretizeType(ctx.gtype, tf->func->type);
  }

  switch (node->kind) {
  case Node::kAdd:
  case Node::kSub:
  case Node::kMul:
  case Node::kDiv:
    dup->type = MergeTypeBinOp(lhs->type, rhs->type);
    break;
  case Node::kEqu:
  case Node::kGT:
    dup->type = node->type;
    break;
  case Node::kBlock:
    break;
  case Node::kRet:
    dup->type = lhs->type;
    break;
  case Node::kIf:
    break;
  case Node::kAssign:
    dup->type = lhs->type;
    break;
  case Node::kCall:
    dup->type = lhs->type->base;
    break;
  case Node::kParam:
    dup->type = lhs->type;
    break;
  case Node::kSizeof:
    dup->type = node->type;
    break;
  case Node::kCast:
    dup->type = ConcretizeType(ctx.gtype, node->type);
    break;
  case Node::kDeref:
    dup->type = lhs->type->base;
    break;
  case Node::kSubscr:
    dup->type = lhs->type->base;
    break;
  case Node::kInc:
  case Node::kDec:
    dup->type = lhs->type;
    break;
  case Node::kArrow:
    if (auto p = GetPrimaryType(lhs->type); p->kind != Type::kPointer) {
      cerr << "lhs must be a pointer to a struct: " << p << endl;
      ErrorAt(ctx.src, *node->token);
    } else if (auto t = GetPrimaryType(p->base); t->kind != Type::kStruct) {
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
  case Node::kTList:
    break;
  default:
    cerr << "ConcretizeNode: not implemented" << endl;
    ErrorAt(ctx.src, *node->token);
  }

  return dup;
}

} // namespace

using DoneKey = std::pair<Type*, TypeMap*>;

struct LessDoneKey {
  bool operator()(const DoneKey& a, const DoneKey& b) const {
    if (a.first < b.first) {
      return true;
    } else if (a.first > b.first) {
      return false;
    }
    // a.first == b.first
    if (b.second == nullptr) {
      return false;
    } else if (a.second == nullptr) {
      return true;
    }
    // a.second != nullptr && b.second != nullptr
    return *a.second < *b.second;
  }
};

using DoneMap = std::map<DoneKey, Type*, LessDoneKey>;

Type* ConcretizeType(TypeMap* gtype, Type* type, DoneMap& done) {
  if (type == nullptr) {
    return nullptr;
  } else if (auto it = done.find({type, gtype}); it != done.end()) {
    return it->second;
  }

  if (type->kind == Type::kConcrete) {
    auto generic_t = GetUserBaseType(type->base);
    auto type_list = type->next;

    TypeMap gtype_;
    auto gparam = generic_t->next;
    for (auto param = type_list; param; param = param->next) {
      string gname{get<Token*>(gparam->value)->raw};
      gtype_[gname] = ConcretizeType(gtype, param->base, done);
      gparam = gparam->next;
    }
    gtype = &gtype_;

    return done[{type, gtype}] = ConcretizeType(gtype, generic_t->base, done);
  }

  if (type->kind == Type::kGParam) {
    if (gtype) {
      return done[{type, gtype}] = (*gtype)[string(get<Token*>(type->value)->raw)];
    }
    return done[{type, gtype}] = type;
  }

  auto dup = NewType(type->kind);
  done[{type, gtype}] = dup;

  dup->base = ConcretizeType(gtype, type->base, done);
  dup->next = ConcretizeType(gtype, type->next, done);
  dup->value = type->value;
  return dup;
}

Type* ConcretizeType(TypeMap& gtype, Type* type) {
  DoneMap done;
  return ConcretizeType(&gtype, type, done);
}

Type* ConcretizeType(Type* type) {
  DoneMap done;
  return ConcretizeType(nullptr, type, done);
}

TypedFunc* NewTypedFunc(ASTContext& ctx, Object* gfunc, Node* type_list) {
  assert(type_list->kind == Node::kTList);

  auto cf = new TypedFunc{{}, gfunc};
  auto gname = gfunc->def->rhs; // generic name list: T, S, ...
  for (auto tnode = type_list->lhs; tnode; tnode = tnode->next) {
    cf->gtype[string(gname->token->raw)] = tnode->type;
    gname = gname->next;
  }

  return cf;
}

Type* ConcretizeType(TypedFunc& f) {
  return ConcretizeType(f.gtype, f.func->type);
}

std::string Mangle(TypedFunc& f) {
  auto t = ConcretizeType(f);
  return Mangle(f.func->id->raw, t);
}

Node* ConcretizeDefFunc(Source& src, TypeMap& gtype, Node* def) {
  assert(def->kind == Node::kDefFunc);
  auto def_dup = NewNode(Node::kDefFunc, def->token);

  for (auto& [ gname, conc_t ] : gtype) {
    gtype[gname] = ConcretizeType(gtype, conc_t);
  }

  auto func = get<Object*>(def->value);
  Type* conc_func_t = ConcretizeType(gtype, func->type);
  char* conc_name = strdup(Mangle(func->id->raw, conc_func_t).c_str());
  auto conc_name_token = new Token{Token::kId, string_view{conc_name}, {}};

  auto obj_dup = NewFunc(conc_name_token, def, func->linkage);
  obj_dup->locals = func->locals;
  map<Object*, Object*> new_lvars;
  for (size_t i = 0; i < func->locals.size(); ++i) {
    Object* lvar = func->locals[i];
    auto t = ConcretizeType(gtype, lvar->type);
    if (t != lvar->type) {
      auto new_lvar = new Object{*lvar};
      new_lvar->type = t;
      obj_dup->locals[i] = new_lvar;
      new_lvars[lvar] = new_lvar;
    }
  }

  obj_dup->type = conc_func_t;
  ConcContext ctx{src, gtype, obj_dup, new_lvars};

  def_dup->lhs = ConcretizeNode(ctx, def->lhs);
  def_dup->rhs = ConcretizeNode(ctx, def->rhs);
  def_dup->cond = ConcretizeNode(ctx, def->cond);

  def_dup->value = obj_dup;

  return def_dup;
}
