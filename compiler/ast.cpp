#include "ast.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>

#include "magic_enum.hpp"
#include "source.hpp"

/*
 * 構文解析（AST生成）時はローカル変数の型を一般に決定できない
 * →スタックの減算量は，第2フェーズで型を決定してから。
 */

using namespace std;

namespace {

Type* NewType(Type::Kind kind, Token* name) {
  return new Type{kind, name, nullptr, nullptr, 0};
}

Type* NewTypePointer(Token* name, Type* base_type) {
  return new Type{Type::kPointer, name, nullptr, base_type, 0};
}

Type* NewTypeInt(Token* name, int64_t num_bits) {
  return new Type{Type::kInt, name, nullptr, nullptr, num_bits};
}

Type* NewTypeUInt(Token* name, int64_t num_bits) {
  return new Type{Type::kUInt, name, nullptr, nullptr, num_bits};
}

Type* NewTypeParam(Token* name, Type* base_type) {
  return new Type{Type::kParam, name, nullptr, base_type, 0};
}

Type* NewTypeFunc(Node* param_list, Node* ret_tspec) {
  auto func_type{NewType(Type::kFunc, nullptr)};
  if (!ret_tspec) {
    func_type->base = NewType(Type::kVoid, nullptr);
  } else {
    func_type->base = ret_tspec->type;
  }

  auto param{param_list->next};
  auto cur{func_type};
  while (param) {
    if (param->tspec->type->kind == Type::kVParam) {
      cur->next = param->tspec->type;
    } else {
      cur->next = NewTypeParam(param->token, param->tspec->type);
    }
    cur = cur->next;
    param = param->next;
  }

  return func_type;
}

Type* NewTypeField(Token* name, Type* base_type) {
  return new Type{Type::kField, name, nullptr, base_type, 0};
}

Type* NewTypeArray(Token* name, Type* base_type, int64_t len) {
  return new Type{Type::kArray, name, nullptr, base_type, len};
}

Type* NewTypeStruct(Token* name, Type* field_list) {
  return new Type{Type::kStruct, name, field_list, nullptr, 0};
}

Type* NewTypeUser(Token* name, Type* base_type) {
  return new Type{Type::kUser, name, nullptr, base_type, 0};
}

Symbol* NewSymbol(Symbol::Kind kind, Token* token) {
  return new Symbol{kind, token, nullptr, nullptr, 0};
}

Node* NewNode(Node::Kind kind, Token* tk) {
  return new Node{kind, tk, nullptr, nullptr, nullptr, nullptr,
                  nullptr, {0}, nullptr};
}

Node* NewNodeExpr(Node::Kind kind, Token* op, Node* lhs, Node* rhs) {
  return new Node{kind, op, nullptr, nullptr, lhs, rhs,
                  nullptr, {0}, nullptr};
}

Node* NewNodeInt(Token* tk, int64_t value, int64_t num_bits) {
  return new Node{Node::kInt, tk, nullptr, nullptr, nullptr, nullptr,
                  nullptr, {value}, NewTypeInt(nullptr, num_bits)};
}

Node* NewNodeType(Token* tk, Type* type) {
  return new Node{Node::kType, tk, nullptr, nullptr, nullptr, nullptr,
                  nullptr, {0}, type};
}

Node* NewNodeCond(Node::Kind kind, Token* tk,
                  Node* cond, Node* lhs, Node* rhs) {
  return new Node{kind, tk, nullptr, cond, lhs, rhs,
                  nullptr, {0}, nullptr};
}

Symbol* AllocateLVar(Context* ctx, Token* lvar_id) {
  auto lvar{NewSymbol(Symbol::kLVar, lvar_id)};
  ctx->local_vars[lvar_id->Raw()] = lvar;
  lvar->ctx = ctx;
  return lvar;
}

void ErrorRedefineID(Token* id) {
  cerr << "'" << id->Raw() << "' is redefined" << endl;
  ErrorAt(id->loc);
}

Context* cur_ctx; // コンパイル中の文や式を含む関数のコンテキスト

const map<Node::Kind, const char*> kUnaryOps{
  {Node::kAddr,  "&"},
  {Node::kDeref, "*"},
};

void RegisterSymbol(Symbol* sym) {
  symbols[sym->token->Raw()] = sym;

  auto it{undeclared_id_nodes.begin()};
  while (it != undeclared_id_nodes.end()) {
    auto node{*it};
    if (sym->token->Raw() == node->token->Raw()) {
      node->value.sym = sym;
      node->type = sym->type;
      it = undeclared_id_nodes.erase(it);
    } else {
      ++it;
    }
  }
}

pair<char*, size_t> DecodeEscapeSequence(Token* tk_str) {
  auto s{tk_str->Raw()};
  if (s[0] != '"') {
    cerr << "invalid string literal" << endl;
    ErrorAt(tk_str->loc);
  }

  auto decoded{new char[s.length() - 2]};
  auto p{decoded};

  for (size_t i{1};;) {
    if (i >= s.length()) {
      cerr << "string literal is not closed" << endl;
      ErrorAt(tk_str->loc + i);
    }
    if (s[i] == '"') {
      return {decoded, static_cast<size_t>(p - decoded)};
    }
    if (s[i] != '\\') {
      *p++ = s[i++];
      continue;
    }
    *p++ = GetEscapeValue(s[i + 1]);
    i += 2;
  }
}

//vector<Node*> unknown_type_nodes;

} // namespace

std::size_t Context::StackSize() const {
  return CalcStackOffset(local_vars, nullptr);
}

Node* Program() {
  auto node{DeclarationSequence()};
  Expect(Token::kEOF);
  return node;
}

Node* DeclarationSequence() {
  auto head{NewNode(Node::kDeclSeq, nullptr)};
  auto cur{head};
  for (;;) {
    cur_ctx = nullptr;
    if (Peek(Token::kFunc)) {
      cur->next = FunctionDefinition();
    } else if (Peek(Token::kExtern)) {
      cur->next = ExternDeclaration();
    } else if (Peek(Token::kVar)) {
      cur->next = VariableDefinition();
    } else if (Peek(Token::kType)) {
      cur->next = TypeDeclaration();
    } else {
      return head;
    }
    while (cur->next) {
      cur = cur->next;
    }
  }
}

Node* FunctionDefinition() {
  auto op{Expect(Token::kFunc)};
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
    auto lvar{AllocateLVar(cur_ctx, param->token)};
    lvar->type = param->tspec->type;
    cur_ctx->params.push_back(lvar);
    param = param->next;
  }

  auto body{CompoundStatement()};
  auto node{NewNode(Node::kDefFunc, name)};
  node->lhs = body;
  node->tspec = NewNodeType(op, NewTypeFunc(plist, ret_tspec));

  auto sym{NewSymbol(Symbol::kFunc, name)};
  sym->type = node->tspec->type;
  RegisterSymbol(sym);
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
  if (!tspec) {
    cerr << "type must be specified" << endl;
    ErrorAt(cur_token->loc);
  }

  Expect(";");

  auto node{NewNode(Node::kExtern, id)};
  node->tspec = tspec;
  node->value.sym = tspec->type->kind == Type::kFunc
                    ? NewSymbol(Symbol::kEFunc, id)
                    : NewSymbol(Symbol::kEVar, id);
  node->value.sym->type = tspec->type;
  RegisterSymbol(node->value.sym);
  return node;
}

Node* Statement() {
  if (auto stmt{JumpStatement()}) {
    return stmt;
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

  if (Peek(Token::kVar)) {
    return VariableDefinition();
  }

  return ExpressionStatement();
}

Node* CompoundStatement() {
  auto head{NewNode(Node::kBlock, Expect("{"))};
  auto cur{head};
  while (!Consume("}")) {
    cur->next = Statement();
    while (cur->next) {
      cur = cur->next;
    }
  }
  return head;
}

Node* SelectionStatement() {
  auto tk{Expect(Token::kIf)};
  auto cond{Expr()};
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
  return NewNodeCond(Node::kIf, tk, cond, body, body_else);
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
  return NewNodeCond(Node::kFor, tk, cond, body, init);
}

Node* JumpStatement() {
  if (auto tk{Consume(Token::kRet)}) {
    auto expr{Expr()};
    Expect(";");
    auto node{NewNode(Node::kRet, tk)};
    node->lhs = expr;
    return node;
  }

  if (auto tk{Consume(Token::kBreak)}) {
    Expect(";");
    return NewNode(Node::kBreak, tk);
  }

  if (auto tk{Consume(Token::kCont)}) {
    Expect(";");
    return NewNode(Node::kCont, tk);
  }

  return nullptr;
}

Node* ExpressionStatement() {
  auto node{Expr()};
  if (auto op{Consume("++")}) {
    node = NewNodeExpr(Node::kInc, op, node, nullptr);
  } else if (auto op{Consume("--")}) {
    node = NewNodeExpr(Node::kDec, op, node, nullptr);
  }
  Expect(";");
  return node;
}

Node* Expr() {
  return Assignment();
}

Node* Assignment() {
  auto node{LogicalOr()};

  const map<string_view, Node::Kind> opmap{
    {"+=", Node::kAdd},
    {"-=", Node::kSub},
    {"*=", Node::kMul},
    {"/=", Node::kDiv},
  };

  // 代入演算は右結合
  if (auto op{Consume("=")}) {
    auto rhs{Assignment()};
    node = NewNodeExpr(Node::kAssign, op, node, rhs);
  } else if (auto it{opmap.find(cur_token->Raw())}; it != opmap.end()) {
    ++cur_token;
    auto rhs{NewNodeExpr(it->second, op, node, Assignment())};
    node = NewNodeExpr(Node::kAssign, op, node, rhs);
  } else if (auto op{Consume(":=")}) {
    if (node->kind == Node::kId) {
      if (auto sym{node->value.sym}) {
        ErrorRedefineID(sym->token);
      }
      auto it = std::remove(undeclared_id_nodes.begin(), undeclared_id_nodes.end(), node);
      undeclared_id_nodes.erase(it, undeclared_id_nodes.end());
    } else {
      cerr << "lhs of ':=' must be an identifier" << endl;
      ErrorAt(node->token->loc);
    }

    auto init{Assignment()};
    auto lvar{AllocateLVar(cur_ctx, node->token)};
    node->value.sym = lvar;
    //node = NewNodeExpr(Node::kAssign, op, node, init, init->type);
    node = NewNodeExpr(Node::kDefVar, op, node, init);
  }
  return node;
}

Node* LogicalOr() {
  auto node{LogicalAnd()};

  for (;;) {
    if (auto op{Consume("||")}) {
      node = NewNodeExpr(Node::kLOr, op, node, LogicalAnd());
    } else {
      return node;
    }
  }
}

Node* LogicalAnd() {
  auto node{Equality()};

  for (;;) {
    if (auto op{Consume("&&")}) {
      node = NewNodeExpr(Node::kLAnd, op, node, Equality());
    } else {
      return node;
    }
  }
}

Node* Equality() {
  auto node{Relational()};

  for (;;) {
    if (auto op{Consume("==")}) {
      node = NewNodeExpr(Node::kEqu, op, node, Relational());
    } else if (auto op{Consume("!=")}) {
      node = NewNodeExpr(Node::kNEqu, op, node, Relational());
    } else {
      return node;
    }
  }
}

Node* Relational() {
  auto node{Additive()};

  for (;;) {
    if (auto op{Consume("<")}) {
      node = NewNodeExpr(Node::kGT, op, Additive(), node);
    } else if (auto op{Consume("<=")}) {
      node = NewNodeExpr(Node::kLE, op, node, Additive());
    } else if (auto op{Consume(">")}) {
      node = NewNodeExpr(Node::kGT, op, node, Additive());
    } else if (auto op{Consume(">=")}) {
      node = NewNodeExpr(Node::kLE, op, Additive(), node);
    } else {
      return node;
    }
  }
}

Node* Additive() {
  auto node{Multiplicative()};

  for (;;) {
    if (auto op{Consume("+")}) {
      node = NewNodeExpr(Node::kAdd, op, node, Multiplicative());
    } else if (auto op{Consume("-")}) {
      node = NewNodeExpr(Node::kSub, op, node, Multiplicative());
    } else {
      return node;
    }
  }
}

Node* Multiplicative() {
  auto node{Unary()};

  for (;;) {
    if (auto op{Consume("*")}) {
      node = NewNodeExpr(Node::kMul, op, node, Unary());
    } else if (auto op{Consume("/")}) {
      node = NewNodeExpr(Node::kDiv, op, node, Unary());
    } else {
      return node;
    }
  }
}

Node* Unary() {
  if (Consume("+")) {
    return Unary();
  } else if (auto op{Consume("-")}) {
    auto zero{NewNodeInt(nullptr, 0, 64)};
    auto node{Unary()};
    return NewNodeExpr(Node::kSub, op, zero, node);
  }

  for (auto [ k, v ] : kUnaryOps) {
    if (auto op{Consume(v)}) {
      return NewNodeExpr(k, op, Unary(), nullptr);
    }
  }

  if (auto op{Consume(Token::kSizeof)}) {
    Expect("(");
    auto arg{TypeSpecifier()};
    if (arg == nullptr) {
      arg = Expr();
    }
    Expect(")");
    return NewNodeExpr(Node::kSizeof, op, arg, nullptr);
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
      node = NewNodeExpr(Node::kCall, op, node, head);
    } else if (auto op{Consume("[")}) { // 配列
      auto index{Expr()};
      Expect("]");
      node = NewNodeExpr(Node::kSubscr, op, node, index);
    } else if (auto op{Consume(".")}) {
      auto field_name{Expect(Token::kId)};
      node = NewNodeExpr(Node::kDot, op, node, NewNode(Node::kId, field_name));
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
    auto node{NewNode(Node::kId, tk)};
    if (auto sym{LookupSymbol(cur_ctx, tk->Raw())}) {
      node->value.sym = sym;
    } else if (auto t = FindType(tk)) {
      node->type = t;
    } else {
      undeclared_id_nodes.push_back(node);
    }
    return node;
  } else if (auto tk{Consume(Token::kStr)}) {
    auto decoded{DecodeEscapeSequence(tk)};
    auto node{NewNode(Node::kStr, tk)};
    node->value.str.data = decoded.first;
    node->value.str.len = decoded.second;
    return node;
  } else if (auto tk{Consume(Token::kChar)}) {
    auto n{NewNodeInt(tk, tk->value, 8)};
    n->type = NewTypeUInt(nullptr, 8);
    return n;
  }

  auto tk{Expect(Token::kInt)};
  return NewNodeInt(tk, tk->value, 64);
}

Node* TypeSpecifier() {
  if (auto op{Consume("[")}) { // 配列
    auto num{Expect(Token::kInt)};
    Expect("]");
    auto node{NewNode(Node::kType, op)};

    auto base_tspec{TypeSpecifier()};
    if (!base_tspec) {
      cerr << "array base type must be specified" << endl;
      ErrorAt(cur_token->loc);
    }
    node->type = NewTypeArray(nullptr, base_tspec->type, num->value);
    return node;
  }

  if (auto tk{Consume("*")}) {
    auto base_tspec{TypeSpecifier()};
    if (!base_tspec) {
      cerr << "pointer base type must be specified" << endl;
      ErrorAt(cur_token->loc);
    }
    auto node{NewNode(Node::kType, tk)};
    node->type = NewTypePointer(nullptr, base_tspec->type);
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

  if (auto tk{Consume(Token::kStruct)}) {
    auto node{NewNode(Node::kType, tk)};
    Type head;
    Type* ft{&head};
    Expect("{");
    while (!Consume("}")) {
      auto name{Expect(Token::kId)};
      auto tspec{TypeSpecifier()};
      Expect(";");
      ft->next = NewTypeField(name, tspec->type);
      ft = ft->next;
    }
    node->type = NewTypeStruct(nullptr, head.next);
    return node;
  }

  if (auto type_name{Consume(Token::kId)}) {
    auto node{NewNode(Node::kType, type_name)};
    auto t{FindType(type_name)};
    if (!t) {
      t = NewType(Type::kUnknown, type_name);
      types[type_name->Raw()] = t;
    }
    node->type = t;
    return node;
  }

  return nullptr;
}

Node* ParameterDeclList() {
  auto head{NewNode(Node::kPList, &*cur_token)};
  auto cur{head};

  vector<Node*> params_untyped;
  for (;;) {
    if (auto op{Consume("...")}) { // 可変長引数
      cur->next = NewNode(Node::kParam, op);
      cur->next->tspec = NewNodeType(op, NewType(Type::kVParam, nullptr));
      return head;
    }

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
    if (!type_spec) {
      cerr << "type must be specified" << endl;
      ErrorAt(cur_token->loc);
    }
    for (auto param : params_untyped) {
      param->tspec = type_spec;
    }
    params_untyped.clear();
    if (!Consume(",")) {
      return head;
    }
  }
}

Node* VariableDefinition() {
  Expect(Token::kVar);

  auto one_def{[]{
    auto id{Expect(Token::kId)};
    auto tspec{TypeSpecifier()};
    Node* init{nullptr};
    if (Consume("=")) {
      if (Peek("{")) {
        init = InitializerList();
      } else {
        init = Expr();
      }
    }
    Expect(";");

    if (cur_ctx && LookupLVar(cur_ctx, id->Raw())) {
      ErrorRedefineID(id);
    } else if (!cur_ctx && LookupSymbol(nullptr, id->Raw())) {
      ErrorRedefineID(id);
    }

    auto node{NewNode(Node::kId, id)};
    Symbol* sym;
    if (cur_ctx) {
      sym = NewSymbol(Symbol::kLVar, id);
      cur_ctx->local_vars[id->Raw()] = sym;
    } else {
      sym = NewSymbol(Symbol::kGVar, id);
      RegisterSymbol(sym);
    }
    node->value.sym = sym;
    node = NewNodeExpr(Node::kDefVar, id, node, init);
    node->tspec = tspec;
    return node;
  }};

  Node head;
  auto cur{&head};
  if (Consume("(")) {
    while (!Consume(")")) {
      cur->next = one_def();
      cur = cur->next;
    }
    return head.next;
  }

  return one_def();
}

Node* TypeDeclaration() {
  Expect(Token::kType);
  auto name_token{Expect(Token::kId)};
  auto tspec{TypeSpecifier()};
  Expect(";");

  auto type{NewTypeUser(name_token, tspec->type)};
  if (auto t{types[name_token->Raw()]}; t == nullptr) {
    types[name_token->Raw()] = type;
  } else if (t->kind == Type::kUnknown) {
    *t = *type;
  }

  auto node{NewNode(Node::kTypedef, name_token)};
  node->tspec = tspec;
  return node;
}

Node* InitializerList() {
  auto op{Expect("{")};
  auto init_list{NewNode(Node::kInitList, op)};
  init_list->value.i = 0;

  auto elem{init_list};
  for (;;) {
    if (Consume("}")) {
      break;
    }
    elem->next = Expr();
    elem = elem->next;
    ++init_list->value.i;
    if (Consume(",")) {
      continue;
    }
  }
  return init_list;
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

size_t Sizeof(Token* tk, Type* type) {
  switch (type->kind) {
  case Type::kInt: // fall-through
  case Type::kUInt:
    if (type->num == 0) {
      cerr << "cannot determine zero size integer " << endl;
      ErrorAt(tk->loc);
    }
    return (type->num + 7) / 8;
  case Type::kPointer:
    return 8;
  case Type::kArray:
    return Sizeof(tk, type->base) * type->num;
  case Type::kUser:
    return Sizeof(tk, type->base);
  case Type::kUnknown:
  case Type::kStruct:
    {
      size_t total_size{0};
      for (auto ft{type->next}; ft; ft = ft->next) {
        total_size += Sizeof(ft->name, ft->base);
      }
      return total_size;
    }
  default:
    cerr << "cannot determine size of " << type << endl;
    ErrorAt(tk->loc);
  }
}

bool SetTypeIntegerBinaryExpr(Node* n) {
  auto l{n->lhs ? n->lhs->type : nullptr},
       r{n->rhs ? n->rhs->type : nullptr};
  if (!IsInteger(l->kind) || !IsInteger(r->kind)) {
    return false;
  }

  if (l->kind == Type::kInt && r->kind == Type::kInt) {
    n->type = NewTypeInt(nullptr, max(l->num, r->num));
  } else if (l->kind == Type::kUInt && r->kind == Type::kUInt) {
    n->type = NewTypeUInt(nullptr, max(l->num, r->num));
  } else {
    if (n->lhs->token->kind == Token::kInt &&
        n->rhs->token->kind == Token::kInt) {
      // 両辺が定数
      auto new_type{NewTypeInt(nullptr, max(l->num, r->num))};
      n->lhs->type = n->rhs->type = new_type;
    } else if (n->lhs->token->kind == Token::kInt) {
      if (IsCastable(n->lhs, r)) {
        n->type = n->lhs->type = r;
      } else {
        cerr << "'" << n->lhs->token->Raw()
             << "' is not castable to " << r << endl;
        ErrorAt(n->lhs->token->loc);
      }
    } else {
      if (IsCastable(n->rhs, l)) {
        n->type = n->rhs->type = l;
      } else {
        cerr << "'" << n->rhs->token->Raw()
             << "' is not castable to " << l << endl;
        ErrorAt(n->rhs->token->loc);
      }
    }
  }
  return true;
}

bool SetSymbolType(Node* n) {
  if (n->type) {
    if (n->type->kind == Type::kUnknown && types[n->type->name->Raw()]) {
      n->type = types[n->type->name->Raw()];
    }
    return false; // 既に型が付いてる
  }

  bool changed{false};
  if (n->kind == Node::kAdd || n->kind == Node::kSub ||
      n->kind == Node::kMul || n->kind == Node::kDiv ||
      n->kind == Node::kEqu || n->kind == Node::kNEqu ||
      n->kind == Node::kGT  || n->kind == Node::kLE ||
      n->kind == Node::kAssign ||
      n->kind == Node::kCall ||
      n->kind == Node::kSubscr ||
      n->kind == Node::kLOr || n->kind == Node::kLAnd) {
    changed |= SetSymbolType(n->lhs);
    changed |= SetSymbolType(n->rhs);
    if (!n->lhs->type || !n->rhs->type) {
      return changed;
    }
  } else if (n->kind == Node::kRet ||
             n->kind == Node::kLoop ||
             n->kind == Node::kAddr ||
             n->kind == Node::kDeref ||
             n->kind == Node::kSizeof ||
             n->kind == Node::kInc ||
             n->kind == Node::kDec) {
    changed |= SetSymbolType(n->lhs);
    if (!n->lhs->type) {
      return changed;
    }
  } else if (n->kind == Node::kFor) {
    if (n->rhs) { // for init; cond; next {} の形
      changed |= SetSymbolType(n->rhs);
      changed |= SetSymbolType(n->rhs->next);
    }
    changed |= SetSymbolType(n->cond);
    changed |= SetSymbolType(n->lhs); // body
    if (!n->lhs->type ||
        (n->rhs && (!n->rhs->type || !n->rhs->next->type))) {
      return changed;
    }
  } else if (n->kind == Node::kIf) {
    changed |= SetSymbolType(n->cond);
    changed |= SetSymbolType(n->lhs);
    if (n->rhs) {
      changed |= SetSymbolType(n->rhs);
    }
    if (!n->lhs->type || (n->rhs && !n->rhs->type)) {
      return changed;
    }
  } else if (n->kind == Node::kDefVar) {
    // kDefVar: tspec か rhs から型を決める
    Type* var_type;
    if (n->rhs) {
      changed |= SetSymbolType(n->rhs);
      if (!n->rhs->type) {
        return changed;
      }
    }
    if (n->tspec) {
      var_type = n->tspec->type;
      if (var_type->kind == Type::kUnknown && var_type->name &&
          types[var_type->name->Raw()]) {
        var_type = types[var_type->name->Raw()];
      }
    } else if (n->rhs) {
      var_type = n->rhs->type;
    } else {
      cerr << "at least either n->tspec or n->rhs must not be null" << endl;
      ErrorAt(n->token->loc);
    }
    n->lhs->value.sym->type = var_type;
    n->lhs->type = var_type;
    n->type = var_type;
    return true;
  } else if (n->kind == Node::kDot) {
    changed |= SetSymbolType(n->lhs);
    auto l{GetEssentialType(n->lhs->type)};
    if (!l) {
      return changed;
    } else if (l->kind != Type::kStruct) {
      cerr << "lhs of . must be a struct: " << l << endl;
      ErrorAt(n->lhs->token->loc);
    } else if (n->rhs->kind != Node::kId) {
      cerr << "COMPILER BUG: rhs of . must be an identifier" << endl;
      ErrorAt(n->rhs->token->loc);
    }
  } else if (n->kind == Node::kType) {
    if (n->tspec->type->kind == Type::kUnknown &&
        n->tspec->type->name && types[n->tspec->type->name->Raw()]) {
      n->tspec->type = types[n->tspec->type->name->Raw()];
      changed |= true;
    }
    return changed;
  }
  auto l{n->lhs ? n->lhs->type : nullptr},
       r{n->rhs ? n->rhs->type : nullptr};

  switch (n->kind) {
  case Node::kAdd:
    if (SetTypeIntegerBinaryExpr(n)) {
      return true;
    }
    if (l->kind == Type::kPointer && r->kind == Type::kInt) {
      n->type = l;
    } else if (l->kind == Type::kInt && r->kind == Type::kPointer) {
      n->type = r;
    } else if (SameType(l, r)) {
      n->type = l;
    } else {
      cerr << "not implemented expression "
           << l << ' ' << n->token->Raw() << ' ' << r << endl;
      ErrorAt(n->token->loc);
    }
    break;
  case Node::kSub:
    if (SetTypeIntegerBinaryExpr(n)) {
      return true;
    }
    if (l->kind == Type::kPointer && r->kind == Type::kInt) {
      n->type = l;
    } else if (l->kind == Type::kPointer && r->kind == Type::kPointer) {
      n->type = NewTypeInt(nullptr, 64);
    } else if (SameType(l, r)) {
      n->type = l;
    } else {
      cerr << "not implemented expression "
           << l << ' ' << n->token->Raw() << ' ' << r << endl;
      ErrorAt(n->token->loc);
    }
    break;
  case Node::kMul:
  case Node::kDiv:
    if (SetTypeIntegerBinaryExpr(n)) {
      return true;
    }
    if (SameType(l, r)) {
      n->type = l;
    } else {
      cerr << "not implemented expression "
           << l << ' ' << n->token->Raw() << ' ' << r << endl;
      ErrorAt(n->token->loc);
    }
    break;
  case Node::kInt:
    n->type = NewTypeInt(nullptr, 64);
    break;
  case Node::kEqu:
  case Node::kNEqu:
  case Node::kGT:
  case Node::kLE:
    if (IsInteger(l->kind) && IsInteger(r->kind)) {
      n->type = NewTypeUInt(nullptr, 1);
    } else {
      cerr << "not implemented expression "
           << l << ' ' << n->token->Raw() << ' ' << r << endl;
      ErrorAt(n->token->loc);
    }
    break;
  case Node::kId:
    if (auto sym{n->value.sym}; sym && sym->type) {
      n->type = sym->type;
      if (n->type->kind == Type::kUnknown && types[n->type->name->Raw()]) {
        n->type = types[n->type->name->Raw()];
      }
    } else {
      return false;
    }
    break;
  case Node::kRet:
    n->type = l;
    break;
  case Node::kIf:
    n->type = l;
    if (!r) {
      break;
    }
    if (l->kind != r->kind ||
        (l->kind == Type::kPointer && l->base->kind != r->base->kind)) {
      cerr << "if statement types (then and else) are incompatible" << endl;
      ErrorAt(n->token->loc);
    }
    break;
  case Node::kLoop:
  case Node::kFor:
    n->type = l;
    break;
  case Node::kAssign:
    if (l->kind != r->kind) {
      cerr << "cannot assign incompatible type " << r << endl;
      cerr << l << endl;
      ErrorAt(n->token->loc);
    }
    if (l->kind == Type::kPointer && l->base->kind != r->base->kind) {
      cerr << "cannot assign incompatible pointer type " << r << endl;
      ErrorAt(n->token->loc);
    }
    n->type = l;
    break;
  case Node::kBlock:
    for (auto stmt{n->next}; stmt; stmt = stmt->next) {
      changed |= SetSymbolType(stmt);
      n->type = stmt->type;
    }
    return changed;
  case Node::kCall:
    if (l->kind == Type::kFunc) {
      n->type = l->base;
    } else if (l->kind == Type::kPointer) {
      if (l->base->kind != Type::kFunc) {
        cerr << "cannot call non-function pointer" << endl;
        ErrorAt(n->token->loc);
      }
      n->type = l->base->base;
    } else if (auto t = FindType(n->lhs->token)) {
      // 型変換
      n->type = t;
    } else {
      cerr << "cannot call value type " << l << endl;
      ErrorAt(n->token->loc);
    }
    break;
  case Node::kEList:
  case Node::kInitList:
    {
      bool all_typed{true};
      for (auto elem{n->next}; elem; elem = elem->next) {
        changed |= SetSymbolType(elem);
        all_typed &= (elem->type != nullptr);
      }
      if (all_typed) {
        n->type = NewType(Type::kUndefined, nullptr);
        changed = true;
      }
    }
    return changed;
  case Node::kType:
  case Node::kPList:
  case Node::kExtern:
  case Node::kBreak:
  case Node::kCont:
  case Node::kTypedef:
    n->type = NewType(Type::kUndefined, nullptr);
    break;
  case Node::kDeclSeq:
    for (auto decl{n->next}; decl; decl = decl->next) {
      if (decl->kind == Node::kDefFunc) {
        changed |= SetSymbolType(decl);
      } else if (decl->kind == Node::kExtern) {
        changed |= SetSymbolType(decl);
      } else if (decl->kind == Node::kDefVar) {
        changed |= SetSymbolType(decl);
      } else if (decl->kind == Node::kTypedef) {
        changed |= SetSymbolType(decl);
      } else {
        cerr << "not implemented" << endl;
        ErrorAt(decl->token->loc);
      }
    }
    return changed;
  case Node::kDefFunc:
    return SetSymbolType(n->lhs);
  case Node::kAddr:
    n->type = NewTypePointer(nullptr, l);
    break;
  case Node::kDeref:
    if (l->kind != Type::kPointer) {
      cerr << "try to dereference non-pointer: " << l << endl;
      ErrorAt(n->token->loc);
    }
    n->type = l->base;
    break;
  case Node::kDefVar:
    // pass
    break;
  case Node::kParam:
    cerr << "COMPILER BUG: a parameter must be typed" << endl;
    ErrorAt(n->token->loc);
    break;
  case Node::kSubscr:
    if (l->kind != Type::kArray && l->kind != Type::kPointer) {
      cerr << "subscription other than array and pointer" << endl;
      ErrorAt(n->token->loc);
    }
    n->type = l->base;
    break;
  case Node::kStr:
    n->type = NewTypeArray(nullptr, NewTypeUInt(nullptr, 8), n->value.str.len);
    break;
  case Node::kSizeof:
    n->type = NewTypeInt(nullptr, 64);
    break;
  case Node::kLOr:
    n->type = NewTypeInt(nullptr, 64);
    break;
  case Node::kLAnd:
    n->type = NewTypeInt(nullptr, 64);
    break;
  case Node::kInc:
  case Node::kDec:
    n->type = l;
    break;
  case Node::kDot:
    for (auto ft{GetEssentialType(l)->next}; ft; ft = ft->next) {
      if (ft->name->Raw() == n->rhs->token->Raw()) {
        n->type = ft->base;
        return true;
      }
    }
    return false;
  }
  return true;
}

std::map<std::string, Type*> builtin_types{
  {"int",  NewTypeInt(nullptr, 64)},
  {"byte", NewTypeUInt(nullptr, 8)},
  {"uint", NewTypeUInt(nullptr, 64)},
};

Type* FindType(Token* tk) {
  auto name = tk->Raw();
  if (name.length() > 3 && strncmp(tk->loc, "int", 3) == 0) {
    char* endp{nullptr};
    long num_bits{strtol(name.c_str() + 3, &endp, 10)};
    if (*endp != '\0') {
      cerr << "integer width must be 10-digits" << endl;
      ErrorAt(tk->loc + 3);
    }
    return NewTypeInt(tk, num_bits);
  } else if (name.length() > 4 && strncmp(tk->loc, "uint", 4) == 0) {
    char* endp{nullptr};
    long num_bits{strtol(name.c_str() + 4, &endp, 10)};
    if (*endp != '\0') {
      cerr << "integer width must be 10-digits" << endl;
      ErrorAt(tk->loc + 4);
    }
    return NewTypeUInt(tk, num_bits);
  }

  if (auto it = builtin_types.find(name); it != builtin_types.end()) {
    return it->second;
  } else if (auto it = types.find(name); it != types.end()) {
    return it->second;
  }
  return nullptr;
}

bool IsInteger(Type::Kind kind) {
  return kind == Type::kInt || kind == Type::kUInt;
}

// 与られた型が整数型なら true を返す。組み込み/ユーザ定義は問わない。
// 戻り値の second はその整数型。ユーザ定義型の場合はベースとなる整数型。
std::pair<bool, Type*> IsInteger(Type* t) {
  if (IsInteger(t->kind)) {
    return {true, t};
  } else if (t->kind == Type::kUser && IsInteger(t->base->kind)) {
    return {true, t->base};
  }
  return {false, nullptr};
}

size_t CalcStackOffset(
    const std::map<std::string, Symbol*>& local_vars,
    std::function<void (Symbol* lvar, size_t offset)> f) {
  size_t offset{0};
  for (auto [ name, lvar ] : local_vars) {
    auto bytes = Sizeof(lvar->token, lvar->type);
    offset += (bytes + 7) & ~UINT64_C(7);
    if (f) {
      f(lvar, offset);
    }
  }
  return offset;
}

ostream& operator<<(ostream& os, Type* t) {
  if (t == nullptr) {
    os << "NULL";
    return os;
  }
  switch (t->kind) {
  case Type::kInt:
    os << "int" << t->num;
    return os;
  case Type::kUInt:
    os << "uint" << t->num;
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
    os << ')' << t->base;
    return os;
  case Type::kVoid:
    os << "void";
    return os;
  default:
    os << '{' << magic_enum::enum_name(t->kind)
       << ",name=" << (t->name ? t->name->Raw() : "NULL")
       << ",next=" << t->next
       << ",base=" << t->base << '}';
    return os;
  }
}

bool SameType(Type* lhs, Type* rhs) {
  if (lhs == rhs) {
    return true;
  } else if (lhs == nullptr || rhs == nullptr) {
    return false;
  }
  return lhs->kind == rhs->kind &&
         SameType(lhs->next, rhs->next) &&
         SameType(lhs->base, rhs->base) &&
         lhs->num == rhs->num;
}

bool IsCastable(Node* int_constant, Type* cast_to) {
  const auto i{static_cast<uint64_t>(int_constant->value.i)};
  if (i == 0) {
    return true;
  }
  int msb = 63;
  for (; msb > 0 && ((i >> msb) & 1) == 0; --msb);
  if (cast_to->kind == Type::kInt || cast_to->kind == Type::kUInt) {
    return (msb + 1) <= cast_to->num;
  }
  return false;
}

Type* GetEssentialType(Type* user_type) {
  if (user_type == nullptr || user_type->kind != Type::kUser) {
    return user_type;
  }
  return user_type->base;
}
