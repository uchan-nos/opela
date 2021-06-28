#include "generics.hpp"

#include <cassert>
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
