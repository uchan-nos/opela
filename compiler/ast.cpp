#include "ast.hpp"

#include <iostream>

#include "magic_enum.hpp"
#include "source.hpp"

using namespace std;

namespace {

Type* NewType(Type::Kind kind) {
  return new Type{kind, nullptr, nullptr, nullptr};
}

Type* NewTypePointer(Type* base_type) {
  return new Type{Type::kPointer, nullptr, base_type, nullptr};
}

Type* NewTypeFunc(Node* param_list, Node* ret_tspec) {
  auto func_type{NewType(Type::kFunc)};
  if (ret_tspec->type->kind == Type::kUndefined) {
    func_type->ret = NewType(Type::kVoid);
  } else {
    func_type->ret = ret_tspec->type;
  }

  auto param{param_list->next};
  auto cur{func_type};
  while (param) {
    cur->next = param->type;
    cur = cur->next;
    param = param->next;
  }

  return func_type;
}

[[maybe_unused]]
ostream& operator<<(ostream& os, Type* t) {
  if (t == nullptr) {
    os << "NULL";
    return os;
  }
  switch (t->kind) {
  case Type::kInt:
    os << "int";
    return os;
  case Type::kPointer:
    os << '*' << t->base;
    return os;
  case Type::kFunc:
    os << "func(";
    for (auto p{t->next}; p; p = p->next) {
      os << p;
      if (p->next) {
        os << ',';
      }
    }
    os << ')' << t->ret;
    return os;
  case Type::kVoid:
    os << "void";
    return os;
  default:
    os << '{' << magic_enum::enum_name(t->kind)
       << ",base=" << t->base
       << ",next=" << t->next
       << ",ret=" << t->ret << '}';
    return os;
  }
}

Symbol* NewSymbol(Symbol::Kind kind, Token* token, Type* type) {
  return new Symbol{kind, token, type, nullptr, 0};
}

Node* NewNode(Node::Kind kind, Token* tk) {
  return new Node{kind, tk, nullptr, nullptr, nullptr, nullptr,
                  {0}, NewType(Type::kUndefined)};
}

Node* NewNodeExpr(Node::Kind kind, Token* op, Node* lhs, Node* rhs,
                  Type* type) {
  return new Node{kind, op, nullptr, nullptr, lhs, rhs, {0}, type};
}

Node* NewNodeInt(Token* tk, int64_t value) {
  return new Node{Node::kInt, tk, nullptr, nullptr, nullptr, nullptr,
                  {value}, NewType(Type::kInt)};
}

void SetNodeSym(Node* node, Symbol* sym) {
  node->value.sym = sym;
  node->type = sym->type;
}

Symbol* AllocateLVar(Context* ctx, Token* lvar_id, Type* lvar_type) {
  auto lvar{NewSymbol(Symbol::kLVar, lvar_id, lvar_type)};
  ctx->local_vars[lvar_id->Raw()] = lvar;

  lvar->ctx = ctx;
  lvar->offset = ctx->StackSize();
  return lvar;
}

void ErrorRedefineLVar(Token* lvar_id) {
  if (generate_mode) {
    cerr << "'" << lvar_id->Raw() << "' is redefined" << endl;
    ErrorAt(lvar_id->loc);
  }
}

Context* cur_ctx; // コンパイル中の文や式を含む関数のコンテキスト

const map<Node::Kind, const char*> kUnaryOps{
  {Node::kAddr,  "&"},
  {Node::kDeref, "*"},
};

const map<string, Type::Kind> kTypes{
  {"int", Type::kInt},
};

} // namespace

Node* Program() {
  auto node{DeclarationSequence()};
  Expect(Token::kEOF);
  return node;
}

Node* DeclarationSequence() {
  auto head{NewNode(Node::kDeclSeq, nullptr)};
  auto cur{head};
  for (;;) {
    if (Peek(Token::kFunc)) {
      cur->next = FunctionDefinition();
    } else if (Peek(Token::kExtern)) {
      cur->next = ExternDeclaration();
    } else {
      return head;
    }
    cur = cur->next;
  }
}

Node* FunctionDefinition() {
  Expect(Token::kFunc);
  auto name{Expect(Token::kId)};

  auto func_name{name->Raw()};
  cur_ctx = new Context{func_name, {}, {}};
  contexts[func_name] = cur_ctx;

  Expect("(");
  auto plist{ParameterDeclList()};
  Expect(")");
  auto ret_tspec{TypeSpecifier()};

  auto param{plist->next};
  while (param) {
    auto lvar{AllocateLVar(cur_ctx, param->token, param->type)};
    cur_ctx->params.push_back(lvar);
    param = param->next;
  }

  auto body{CompoundStatement()};
  auto node{NewNode(Node::kDefFunc, name)};
  node->lhs = body;

  auto type{NewTypeFunc(plist, ret_tspec)};
  symbols[name->Raw()] = NewSymbol(Symbol::kFunc, name, type);
  return node;
}

Node* ExternDeclaration() {
  Expect(Token::kExtern);
  auto attr{Expect(Token::kStr)};
  if (attr->Raw() != R"("C")") {
    cerr << "unknown attribute " << attr->Raw() << endl;
    ErrorAt(attr->loc);
  }

  auto id{Expect(Token::kId)};
  auto tspec{TypeSpecifier()};
  Expect(";");

  auto node{NewNode(Node::kExtern, id)};
  node->type = tspec->type;
  if (tspec->type->kind == Type::kFunc) {
    symbols[id->Raw()] = NewSymbol(Symbol::kEFunc, id, tspec->type);
  } else {
    symbols[id->Raw()] = NewSymbol(Symbol::kEVar, id, tspec->type);
  }
  return node;
}

Node* Statement() {
  if (Peek(Token::kRet)) {
    return JumpStatement();
  }

  if (Peek(Token::kIf)) {
    return SelectionStatement();
  }

  if (Peek(Token::kFor)) {
    return IterationStatement();
  }

  if (Peek("{")) {
    return CompoundStatement();
  }

  if (Consume(Token::kVar)) {
    auto id{Expect(Token::kId)};
    auto tspec{TypeSpecifier()};
    Node* init{nullptr};
    if (Consume("=")) {
      init = Expr();
    }
    Expect(";");

    if (LookupLVar(cur_ctx, id->Raw())) {
      ErrorRedefineLVar(id);
    }
    auto lvar{AllocateLVar(cur_ctx, id, tspec->type)};
    auto node{NewNode(Node::kId, id)};
    SetNodeSym(node, lvar);
    return new Node{Node::kDefVar, id, nullptr, tspec, node, init,
                    {0}, tspec->type};
  }

  return ExpressionStatement();
}

Node* CompoundStatement() {
  auto head{NewNode(Node::kBlock, Expect("{"))};
  auto cur{head};
  while (!Consume("}")) {
    cur->next = Statement();
    cur = cur->next;
  }
  return head;
}

Node* SelectionStatement() {
  auto tk{Expect(Token::kIf)};
  auto expr{Expr()};
  auto body{CompoundStatement()};
  Node* body_else{nullptr};
  if (Consume(Token::kElse)) {
    if (Peek("{")) {
      body_else = CompoundStatement();
    } else if (Peek(Token::kIf)) {
      body_else = SelectionStatement();
    } else {
      cerr << "else must be followed by if/compound statement" << endl;
      ErrorAt(cur_token->loc);
    }
  }
  return new Node{Node::kIf, tk, nullptr, expr, body, body_else,
                  {0}, NewType(Type::kUndefined)};
}

Node* IterationStatement() {
  auto tk{Expect(Token::kFor)};
  if (Peek("{")) {
    auto body{CompoundStatement()};
    auto node{NewNode(Node::kLoop, tk)};
    node->lhs = body;
    return node;
  }

  auto cond{Expr()};
  Node* init{nullptr};
  if (Consume(";")) {
    init = cond;
    cond = Expr();
    Expect(";");
    init->next = Expr();
  }
  auto body{CompoundStatement()};
  return new Node{Node::kFor, tk, nullptr, cond, body, init,
                  {0}, NewType(Type::kUndefined)};
}

Node* JumpStatement() {
  auto tk{Expect(Token::kRet)};
  auto expr{Expr()};
  Expect(";");
  auto node{NewNode(Node::kRet, tk)};
  node->lhs = expr;
  return node;
}

Node* ExpressionStatement() {
  auto node{Expr()};
  Expect(";");
  return node;
}

Node* Expr() {
  return Assignment();
}

Node* Assignment() {
  auto node{Equality()};

  // 代入演算は右結合
  if (auto op{Consume("=")}) {
    node = NewNodeExpr(Node::kAssign, op, node, Assignment(), node->type);
  } else if (auto op{Consume(":=")}) {
    if (node->kind == Node::kId) {
      if (auto sym{node->value.sym}) {
        ErrorRedefineLVar(sym->token);
      }
    } else {
      cerr << "lhs of ':=' must be an identifier" << endl;
      ErrorAt(node->token->loc);
    }

    auto init{Assignment()};
    auto lvar{AllocateLVar(cur_ctx, node->token, init->type)};
    SetNodeSym(node, lvar);
    node = NewNodeExpr(Node::kAssign, op, node, init, init->type);
  }
  return node;
}

Node* Equality() {
  auto node{Relational()};

  for (;;) {
    if (auto op{Consume("==")}) {
      node = NewNodeExpr(Node::kEqu, op, node, Relational(),
                         NewType(Type::kInt));
    } else if (auto op{Consume("!=")}) {
      node = NewNodeExpr(Node::kNEqu, op, node, Relational(),
                         NewType(Type::kInt));
    } else {
      return node;
    }
  }
}

Node* Relational() {
  auto node{Additive()};

  auto check_types{[&](Node* rel_node){
    if (generate_mode &&
        rel_node->lhs->type->kind != rel_node->rhs->type->kind) {
      cerr << "incompatible types for " << rel_node->token->Raw() << endl;
      ErrorAt(rel_node->token->loc);
    }
  }};

  for (;;) {
    if (auto op{Consume("<")}) {
      node = NewNodeExpr(Node::kLT, op, node, Additive(), NewType(Type::kInt));
      check_types(node);
    } else if (auto op{Consume("<=")}) {
      node = NewNodeExpr(Node::kLE, op, node, Additive(), NewType(Type::kInt));
      check_types(node);
    } else if (auto op{Consume(">")}) {
      node = NewNodeExpr(Node::kLT, op, Additive(), node, NewType(Type::kInt));
      check_types(node);
    } else if (auto op{Consume(">=")}) {
      node = NewNodeExpr(Node::kLE, op, Additive(), node, NewType(Type::kInt));
      check_types(node);
    } else {
      return node;
    }
  }
}

Node* Additive() {
  auto node{Multiplicative()};

  auto merge_types{[&](Token* op, Type* rhs_type){
    if (generate_mode &&
        (node->type->kind != Type::kInt || rhs_type->kind != Type::kInt)) {
      cerr << "not implemented expression 'x " << op->Raw()
           << " y' for non-int operand" << endl;
      ErrorAt(op->loc);
    }
    return rhs_type;
  }};

  for (;;) {
    // TODO merge both types of lhs and rhs

    if (auto op{Consume("+")}) {
      auto rhs{Multiplicative()};
      auto result_type{merge_types(op, rhs->type)};
      node = NewNodeExpr(Node::kAdd, op, node, rhs, result_type);
    } else if (auto op{Consume("-")}) {
      auto rhs{Multiplicative()};
      auto result_type{merge_types(op, rhs->type)};
      node = NewNodeExpr(Node::kSub, op, node, rhs, result_type);
    } else {
      return node;
    }
  }
}

Node* Multiplicative() {
  auto node{Unary()};

  for (;;) {
    // TODO merge both types of lhs and rhs
    if (auto op{Consume("*")}) {
      node = NewNodeExpr(Node::kMul, op, node, Unary(), node->type);
    } else if (auto op{Consume("/")}) {
      node = NewNodeExpr(Node::kDiv, op, node, Unary(), node->type);
    } else {
      return node;
    }
  }
}

Node* Unary() {
  if (Consume("+")) {
    return Unary();
  } else if (auto op{Consume("-")}) {
    auto zero{NewNodeInt(nullptr, 0)};
    auto node{Unary()};
    return NewNodeExpr(Node::kSub, op, zero, node, node->type);
  }

  for (auto [ k, v ] : kUnaryOps) {
    if (auto op{Consume(v)}) {
      auto node{Unary()};
      auto type{node->type};
      if (k == Node::kDeref) {
        if (node->type->kind != Type::kPointer) {
          if (generate_mode) {
            cerr << "try to dereference non-pointer" << endl;
            ErrorAt(node->token->loc);
          }
        }
        type = type->base;
      } else if (k == Node::kAddr) {
        type = NewTypePointer(type);
      }
      return NewNodeExpr(k, op, node, nullptr, type);
    }
  }

  return Postfix();
}

Node* Postfix() {
  auto node{Primary()};

  for (;;) {
    if (auto op{Consume("(")}) {
      auto head{NewNode(Node::kEList, op)};
      auto cur{head};
      if (!Consume(")")) {
        for (;;) {
          cur->next = Expr();
          cur = cur->next;
          if (!Consume(",")) {
            Expect(")");
            break;
          }
        }
      }
      // TODO: get the return type of a function
      Type* ret_type{nullptr};
      switch (node->type->kind) {
      case Type::kFunc:
        ret_type = node->type->ret;
        break;
      case Type::kPointer:
        ret_type = node->type->base->ret;
        break;
      default:
        if (generate_mode) {
          cerr << "cannot call value type " << node->type << endl;
          ErrorAt(node->token->loc);
        }
      }
      node = NewNodeExpr(Node::kCall, op, node, head, ret_type);
    } else {
      return node;
    }
  }
}

Node* Primary() {
  if (Consume("(")) {
    auto node{Expr()};
    Expect(")");
    return node;
  } else if (auto tk{Consume(Token::kId)}) {
    if (auto sym{LookupSymbol(cur_ctx, tk->Raw())}) {
      auto node{NewNode(Node::kId, sym->token)};
      SetNodeSym(node, sym);
      return node;
    }
    return NewNode(Node::kId, tk);
  }

  auto tk{Expect(Token::kInt)};
  return NewNodeInt(tk, tk->value);
}

Node* TypeSpecifier() {
  if (auto tk{Consume("*")}) {
    auto base_tspec{TypeSpecifier()};
    auto node{NewNode(Node::kType, tk)};
    node->type = NewTypePointer(base_tspec->type);
    return node;
  }

  if (auto tk{Consume(Token::kFunc)}) {
    Expect("(");
    auto plist{ParameterDeclList()};
    Expect(")");
    auto ret_tspec{TypeSpecifier()};

    auto node{NewNode(Node::kType, tk)};
    node->type = NewTypeFunc(plist, ret_tspec);
    return node;
  }

  if (auto type_name{Consume(Token::kId)}) {
    auto it{kTypes.find(type_name->Raw())};
    if (it == kTypes.end()) {
      cerr << "unknown type " << type_name->Raw() << endl;
      ErrorAt(type_name->loc);
    }
    auto node{NewNode(Node::kType, type_name)};
    node->type = NewType(it->second);
    return node;
  }

  // 型が読み取れなかったときは，nullptr ではなく Type::kUndefined を返す
  auto node{NewNode(Node::kType, nullptr)};
  node->type = NewType(Type::kUndefined);
  return node;
}

Node* ParameterDeclList() {
  auto head{NewNode(Node::kPList, &*cur_token)};
  auto cur{head};

  vector<Node*> params_untyped;
  for (;;) {
    auto param_name{Consume(Token::kId)};
    if (param_name == nullptr) {
      return head;
    }

    cur->next = NewNode(Node::kParam, param_name);
    cur = cur->next;
    params_untyped.push_back(cur);

    if (Consume(",")) {
      continue;
    }

    auto type_spec{TypeSpecifier()};
    for (auto param : params_untyped) {
      param->type = type_spec->type;
    }
    params_untyped.clear();
  }
}

Symbol* LookupLVar(Context* ctx, const std::string& name) {
  if (auto it{ctx->local_vars.find(name)}; it != ctx->local_vars.end()) {
    return it->second;
  }
  return nullptr;
}

Symbol* LookupSymbol(Context* ctx, const std::string& name) {
  if (ctx != nullptr) {
    if (auto it{ctx->local_vars.find(name)}; it != ctx->local_vars.end()) {
      return it->second;
    }
  }
  if (auto it{symbols.find(name)}; it != symbols.end()) {
    return it->second;
  }
  return nullptr;
}
