#include "generics.hpp"

#include <cassert>
#include <cstring>
#include <functional>
#include <iostream>
#include <sstream>

#include "ast.hpp"

using namespace std;

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

std::string Mangle(ConcreteFunc& f) {
  return Mangle(f.func->id->raw, CalcConcreteType(f));
}

std::string Mangle(Source& src, ConcreteFunc& f) {
  return Mangle(f.func->id->raw, CalcConcreteType(src, f));
}

Type* CalcConcreteType(ConcreteFunc& f, std::function<void (Token*)> err_at) {
  auto param_type_list = NewType(Type::kInt); // dummy;
  auto cur = param_type_list;
  for (auto pt = f.func->type->next; pt; pt = pt->next) {
    if (pt->base->kind == Type::kGeneric) {
      auto generic_name = get<Token*>(pt->base->value);
      if (auto it = f.gtype.find(string{generic_name->raw});
          it != f.gtype.end()) {
        cur->next = NewTypeParam(it->second, get<Token*>(pt->value));
        cerr << "  param is generic: " << get<Token*>(pt->value)->raw << endl;
      } else {
        cerr << "unknown generic type name" << endl;
        if (err_at) { err_at(generic_name); };
      }
    } else {
      cur->next = pt;
      cerr << "  param is normal: " << get<Token*>(pt->value)->raw << endl;
    }
    cerr << " param type: " << cur->next << endl;
    cur = cur->next;
  }
  cerr << "param type list: ";
  for (auto pt = param_type_list->next; pt; pt = pt->next) {
    cerr << pt << ',';
  }
  cerr << endl;

  auto ret_t = f.func->type->base;
  if (ret_t->kind == Type::kGeneric) {
    auto generic_name = get<Token*>(ret_t->value);
    if (auto it = f.gtype.find(string{generic_name->raw});
        it != f.gtype.end()) {
      ret_t = it->second;
      cerr << "  ret type is generic" << endl;
    } else {
      cerr << "unknown generic type name" << endl;
      if (err_at) { err_at(generic_name); };
    }
  }

  return NewTypeFunc(ret_t, param_type_list->next);
}

Type* CalcConcreteType(ConcreteFunc& f) {
  return CalcConcreteType(f, nullptr);
}

Type* CalcConcreteType(Source& src, ConcreteFunc& f) {
  return CalcConcreteType(f, [&src](Token* token){ ErrorAt(src, *token); });
}

ConcreteFunc* ConcretizeFunc(ASTContext& ctx, Object* gfunc, Node* type_list) {
  assert(type_list->kind == Node::kTList);

  auto cf = new ConcreteFunc{gfunc, {}};
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


Type* ConcretizeType(ConcContext& ctx, Type* type) {
  if (type == nullptr) {
    return nullptr;
  }
  if (type->base == nullptr && type->next == nullptr) {
    if (type->kind != Type::kGeneric) {
      return type;
    }
    return ctx.gtype[string(get<Token*>(type->value)->raw)];
  }

  auto base_dup = ConcretizeType(ctx, type->base);
  auto next_dup = ConcretizeType(ctx, type->next);
  if (base_dup == type->base && next_dup == type->next) {
    return type;
  }

  auto dup = NewType(type->kind);
  dup->base = base_dup;
  dup->next = next_dup;
  dup->value = type->value;
  return dup;
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
    dup = NewNodeType(node->token, ConcretizeType(ctx, node->type));
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
      dup->type = obj_dup->type = ConcretizeType(ctx, obj->type);
    } else {
      dup->type = ConcretizeType(ctx, node->type);
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

Node* ConcretizeDefFunc(ConcContext& ctx, Node* def) {
  assert(def->kind == Node::kDefFunc);
  auto def_dup = NewNode(Node::kDefFunc, def->token);

  auto func = get<Object*>(def->value);
  Type* conc_func_t = ConcretizeType(ctx, func->type);
  char* conc_name = strdup(Mangle(func->id->raw, conc_func_t).c_str());
  auto conc_name_token = new Token{Token::kId, string_view{conc_name}, {}};

  auto obj_dup = NewFunc(conc_name_token, def, func->linkage);
  obj_dup->locals = func->locals;
  obj_dup->type = conc_func_t;
  ctx.func = obj_dup;

  def_dup->lhs = ConcretizeNode(ctx, def->lhs);
  def_dup->rhs = ConcretizeNode(ctx, def->rhs);
  def_dup->cond = ConcretizeNode(ctx, def->cond);

  //obj_dup->type = NewTypeFunc(def_dup->cond->type, ParamTypeFromDeclList(def_dup->rhs));
  def_dup->value = obj_dup;

  return def_dup;
}
