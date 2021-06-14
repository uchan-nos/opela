#include "ast.hpp"

#include <algorithm>
#include <map>
#include <iostream>
#include <sstream>
#include <string>

#include "magic_enum.hpp"
#include "object.hpp"

using namespace std;

namespace {

Node* NewNode(Node::Kind kind, Token* token) {
  return new Node{kind, token, nullptr, nullptr, nullptr, nullptr};
}

Node* NewNodeInt(Token* token, opela_type::Int value) {
  auto node = NewNode(Node::kInt, token);
  node->value = value;
  return node;
}

Node* NewNodeBinOp(Node::Kind kind, Token* op, Node* lhs, Node* rhs) {
  auto node = NewNode(kind, op);
  node->lhs = lhs;
  node->rhs = rhs;
  return node;
}

Node* NewNodeOneChild(Node::Kind kind, Token* token, Node* child) {
  auto node = NewNode(kind, token);
  node->lhs = child;
  return node;
}

Node* NewNodeCond(Node::Kind kind, Token* token,
                  Node* cond, Node* lhs, Node* rhs) {
  auto node = NewNodeBinOp(kind, token, lhs, rhs);
  node->cond = cond;
  return node;
}

Node* NewNodeType(Token* token, Type* type) {
  auto node = NewNode(Node::kType, token);
  node->type = type;
  return node;
}

Node* NewNodeType(ASTContext& ctx, Token* token) {
  auto t = ctx.tm.Find(*token);
  if (t == nullptr) {
    t = NewTypeUnresolved(token);
    cerr << "not implemented: unresolved type handling" << endl;
    ErrorAt(ctx.src, *token);
  }
  return NewNodeType(token, t);
}

Node* NewNodeStr(ASTContext& ctx, Token* str) {
  auto node = NewNode(Node::kStr, str);
  node->value = StringIndex{ctx.strings.size()};
  ctx.strings.push_back(DecodeEscapeSequence(ctx.src, *str));
  return node;
}

Node* NewNodeChar(Token* ch) {
  auto node = NewNode(Node::kChar, ch);
  node->value = get<opela_type::Byte>(ch->value);
  return node;
}

map<Node*, size_t> node_number;
size_t NodeNo(Node* node) {
  if (auto it = node_number.find(node); it != node_number.end()) {
    return it->second;
  }
  auto n = node_number.size();
  node_number.insert({node, n});
  return n;
}

std::string NodeName(Node* node) {
  if (node == nullptr) {
    return "null";
  }
  ostringstream oss;
  oss << "Node_" << NodeNo(node);
  return oss.str();
}

struct NodeValuePrinter {
  std::ostream& os;

  void operator()(VariantDummyType) {
    os << "none";
  }
  void operator()(opela_type::Int v) {
    os << v;
  }
  void operator()(StringIndex v) {
    os << "STR" << v.i;
  }
  void operator()(Object* v) {
    os << v;
  }
};

void PrintAST(std::ostream& os, Node* ast, int indent, bool recursive) {
  if (ast == nullptr) {
    os << "null";
    return;
  }

  os << NodeName(ast) << ' ' << reinterpret_cast<void*>(ast)
     << '{' << magic_enum::enum_name(ast->kind) << ' ';
  if (ast->token) {
    os << "'" << ast->token->raw << "'";
  } else {
    os << "null-token";
  }

  const bool multiline = recursive && (
      ast->type || ast->lhs || ast->rhs || ast->cond || ast->next);
  if (multiline) {
    if (ast->type) {
      os << '\n' << string(indent + 2, ' ') << "type=" << ast->type;
    }
    if (ast->lhs) {
      os << '\n' << string(indent + 2, ' ') << "lhs=";
      PrintAST(os, ast->lhs, indent + 2, recursive);
    }
    if (ast->rhs) {
      os << '\n' << string(indent + 2, ' ') << "rhs=";
      PrintAST(os, ast->rhs, indent + 2, recursive);
    }
    if (ast->cond) {
      os << '\n' << string(indent + 2, ' ') << "cond=";
      PrintAST(os, ast->cond, indent + 2, recursive);
    }
    if (ast->next) {
      os << '\n' << string(indent + 2, ' ') << "next=";
      PrintAST(os, ast->next, indent + 2, recursive);
    }
    if (!get_if<VariantDummyType>(&ast->value)) {
      os << '\n' << string(indent + 2, ' ') << "value=";
      visit(NodeValuePrinter{os}, ast->value);
    }
  } else {
    ast->type && os << " type=" << ast->type;
    ast->lhs && os << " lhs=" << NodeName(ast->lhs);
    ast->rhs && os << " rhs=" << NodeName(ast->rhs);
    ast->cond && os << " cond=" << NodeName(ast->cond);
    ast->next && os << " next=" << NodeName(ast->next);
    if (!get_if<VariantDummyType>(&ast->value)) {
      os << " value=";
      visit(NodeValuePrinter{os}, ast->value);
    }
  }

  if (multiline) {
    os << '\n' << string(indent, ' ') << '}';
  } else {
    os << '}';
  }
}

Object* AllocateLVar(ASTContext& ctx, Token* name, Node* def) {
  if (ctx.sc.FindObjectCurrentBlock(name->raw)) {
    cerr << "local variable is redefined" << endl;
    ErrorAt(ctx.src, *name);
  }
  auto lvar = NewVar(name, def, Object::kLocal);
  ctx.locals->push_back(lvar);
  ctx.sc.PutObject(lvar);
  return lvar;
}

const map<char, Node::Kind> kUnaryOps{
  {'&', Node::kAddr},
  {'*', Node::kDeref},
};

Type* ParamTypeFromDeclList(Node* plist) {
  if (plist == nullptr) {
    return nullptr;
  }

  auto param_type = NewTypeParam(plist->lhs->type, plist->token);
  auto cur = param_type;
  plist = plist->next;
  while (plist) {
    cur->next = NewTypeParam(plist->lhs->type, plist->token);
    cur = cur->next;
    plist = plist->next;
  }
  return param_type;
}

} // namespace

Node* Program(ASTContext& ctx) {
  auto node = DeclarationSequence(ctx);
  ctx.t.Expect(Token::kEOF);
  return node;
}

Node* DeclarationSequence(ASTContext& ctx) {
  auto head = NewNode(Node::kInt, nullptr); // dummy
  auto cur = head;
  for (;;) {
    if (ctx.t.Peek(Token::kFunc)) {
      cur->next = FunctionDefinition(ctx);
    } else if (ctx.t.Peek(Token::kExtern)) {
      cur->next = ExternDeclaration(ctx);
    } else if (ctx.t.Peek(Token::kType)) {
      cur->next = TypeDeclaration(ctx);
    } else if (ctx.t.Peek(Token::kVar)) {
      cur->next = VariableDefinition(ctx);
    } else {
      return head->next;
    }
    while (cur->next) {
      cur = cur->next;
    }
  }
}

Node* FunctionDefinition(ASTContext& ctx) {
  ctx.t.Expect(Token::kFunc);
  auto name = ctx.t.Expect(Token::kId);
  auto node = NewNode(Node::kDefFunc, name);
  auto func_obj = NewFunc(name, node, Object::kGlobal);
  node->value = func_obj;

  ctx.t.Expect("(");
  node->rhs = ParameterDeclList(ctx);
  ctx.t.Expect(")");

  node->cond = TypeSpecifier(ctx); // 戻り値の型情報
  if (node->cond == nullptr) {
    node->cond = NewNode(Node::kType, nullptr);
    node->cond->type = ctx.tm.Find("void");
  }

  ctx.sc.PutObject(func_obj);

  ctx.sc.Enter();
  ASTContext func_ctx{ctx.src, ctx.t, ctx.tm, ctx.sc, ctx.strings,
                      ctx.unresolved_types, ctx.undeclared_ids,
                      &func_obj->locals};

  for (auto param = node->rhs; param; param = param->next) {
    auto var = AllocateLVar(func_ctx, param->token, param);
    param->value = var;
  }

  node->lhs = CompoundStatement(func_ctx);
  ctx.sc.Leave();
  return node;
}

Node* ExternDeclaration(ASTContext& ctx) {
  ctx.t.Expect(Token::kExtern);
  auto attr = ctx.t.Consume(Token::kStr);
  if (attr && attr->raw != R"("C")") {
    cerr << "unknown attribute" << endl;
    ErrorAt(ctx.src, *attr);
  }

  auto id = ctx.t.Expect(Token::kId);
  auto tspec = TypeSpecifier(ctx);
  auto colon_token = ctx.t.Expect(";");
  if (tspec == nullptr) {
    cerr << "type must be specified" << endl;
    ErrorAt(ctx.src, *colon_token);
  }

  auto node = NewNodeOneChild(Node::kExtern, id, tspec);
  node->cond = attr ? NewNodeStr(ctx, attr) : nullptr;
  auto obj = NewFunc(id, node, Object::kExternal);
  node->value = obj;
  ctx.sc.PutObject(obj);
  return node;
}

Node* TypeDeclaration(ASTContext& ctx) {
  ctx.t.Expect(Token::kType);
  auto name_token = ctx.t.Expect(Token::kId);
  auto tspec = TypeSpecifier(ctx);
  ctx.t.Expect(";");

  auto type = NewTypeUser(tspec->type, name_token);
  if (auto prev = ctx.tm.Register(type)) {
    cerr << "type is re-defined: name=" << name_token->raw
         << ", prev=" << prev << endl;
    ErrorAt(ctx.src, *name_token);
  }

  return NewNodeOneChild(Node::kTypedef, name_token, tspec);
}

Node* VariableDefinition(ASTContext& ctx) {
  ctx.t.Expect(Token::kVar);

  auto one_def = [&ctx]{
    auto id = ctx.t.Expect(Token::kId);
    auto tspec = TypeSpecifier(ctx);
    Node* init = nullptr;
    if (ctx.t.Consume("=")) {
      init = Expression(ctx);
    }
    if (init == nullptr && tspec == nullptr) {
      cerr << "initial value or type specifier must be specified" << endl;
      ErrorAt(ctx.src, *id);
    }

    auto id_node = NewNode(Node::kId, id);
    auto def_node = NewNodeBinOp(Node::kDefVar, id, id_node, init);
    def_node->cond = tspec;

    Object* var;
    if (ctx.locals) { // ローカル
      var = AllocateLVar(ctx, id, def_node);
    } else { // グローバル
      var = NewVar(id, def_node, Object::kGlobal);
      ctx.sc.PutObject(var);
    }
    id_node->value = var;

    return def_node;
  };

  auto head = NewNode(Node::kInt, nullptr); // dummy
  auto cur = head;
  if (ctx.t.Consume("(")) {
    for (;;) {
      cur->next = one_def();
      cur = cur->next;
      if (ctx.t.Consume(",")) {
        if (ctx.t.Consume(")")) {
          break;
        }
      } else {
        ctx.t.Expect(")");
        break;
      }
    }
    return head->next;
  }

  auto node = one_def();
  ctx.t.Expect(";");
  return node;
}

Node* Statement(ASTContext& ctx) {
  if (ctx.t.Peek("{")) {
    return CompoundStatement(ctx);
  }
  if (auto token = ctx.t.Consume(Token::kRet)) {
    auto node = NewNodeOneChild(Node::kRet, token, ExpressionStatement(ctx));
    return node;
  }
  if (ctx.t.Peek(Token::kIf)) {
    return SelectionStatement(ctx);
  }
  if (ctx.t.Peek(Token::kFor)) {
    return IterationStatement(ctx);
  }
  if (ctx.t.Peek(Token::kVar)) {
    return VariableDefinition(ctx);
  }
  if (auto token = ctx.t.Consume(Token::kBreak)) {
    ctx.t.Expect(";");
    return NewNode(Node::kBreak, token);
  }
  if (auto token = ctx.t.Consume(Token::kCont)) {
    ctx.t.Expect(";");
    return NewNode(Node::kCont, token);
  }

  return ExpressionStatement(ctx);
}

Node* CompoundStatement(ASTContext& ctx) {
  ctx.sc.Enter();

  auto node = NewNode(Node::kBlock, ctx.t.Expect("{"));
  auto cur = node;
  while (!ctx.t.Consume("}")) {
    cur->next = Statement(ctx);
    while (cur->next) {
      cur = cur->next;
    }
  }

  ctx.sc.Leave();
  return node;
}

Node* SelectionStatement(ASTContext& ctx) {
  auto if_token = ctx.t.Expect(Token::kIf);
  auto cond = Expression(ctx);
  auto body_then = CompoundStatement(ctx);
  Node* body_else = nullptr;
  if (ctx.t.Consume(Token::kElse)) {
    if (ctx.t.Peek(Token::kIf)) {
      body_else = SelectionStatement(ctx);
    } else {
      body_else = CompoundStatement(ctx);
    }
  }
  return NewNodeCond(Node::kIf, if_token, cond, body_then, body_else);
}

Node* IterationStatement(ASTContext& ctx) {
  auto for_token = ctx.t.Expect(Token::kFor);
  if (ctx.t.Peek("{")) {
    auto node = NewNode(Node::kLoop, for_token);
    node->lhs = CompoundStatement(ctx);
    return node;
  }

  auto cond = Expression(ctx);
  Node* init = nullptr;
  if (ctx.t.Consume(";")) {
    init = cond;
    cond = Expression(ctx);
    ctx.t.Expect(";");
    init->next = Expression(ctx);
  }
  auto body = CompoundStatement(ctx);
  return NewNodeCond(Node::kFor, for_token, cond, body, init);
}

Node* ExpressionStatement(ASTContext& ctx) {
  auto node = Expression(ctx);
  ctx.t.Expect(";");
  return node;
}

Node* Expression(ASTContext& ctx) {
  return Assignment(ctx);
}

Node* Assignment(ASTContext& ctx) {
  const map<string_view, Node::Kind> opmap{
    {"+=", Node::kAdd},
    {"-=", Node::kSub},
    {"*=", Node::kMul},
    {"/=", Node::kDiv},
  };

  auto node = LogicalOr(ctx);

  if (auto op = ctx.t.Consume("=")) {
    node = NewNodeBinOp(Node::kAssign, op, node, Assignment(ctx));
  } else if (auto it = opmap.find(ctx.t.Peek()->raw); it != opmap.end()) {
    auto op = ctx.t.Consume();
    auto rhs = NewNodeBinOp(it->second, op, node, Assignment(ctx));
    node = NewNodeBinOp(Node::kAssign, op, node, rhs);
  } else if (auto op = ctx.t.Consume(":=")) {
    if (node->kind != Node::kId) {
      cerr << "lhs of ':=' must be an identifier" << endl;
      ctx.t.Unexpected(*node->token);
    }
    ctx.undeclared_ids.remove(node);
    auto def_node = NewNodeBinOp(Node::kDefVar, op, node, Assignment(ctx));

    auto lvar = AllocateLVar(ctx, node->token, def_node);
    node->value = lvar;

    node = def_node;
  }

  return node;
}

Node* LogicalOr(ASTContext& ctx) {
  auto node = LogicalAnd(ctx);

  for (;;) {
    if (auto op = ctx.t.Consume("||")) {
      node = NewNodeBinOp(Node::kLOr, op, node, LogicalAnd(ctx));
    } else {
      return node;
    }
  }
}

Node* LogicalAnd(ASTContext& ctx) {
  auto node = Equality(ctx);

  for (;;) {
    if (auto op = ctx.t.Consume("&&")) {
      node = NewNodeBinOp(Node::kLAnd, op, node, Equality(ctx));
    } else {
      return node;
    }
  }
}

Node* Equality(ASTContext& ctx) {
  auto node = Relational(ctx);

  for (;;) {
    if (auto op = ctx.t.Consume("==")) {
      node = NewNodeBinOp(Node::kEqu, op, node, Relational(ctx));
    } else if (auto op = ctx.t.Consume("!=")) {
      node = NewNodeBinOp(Node::kNEqu, op, node, Relational(ctx));
    } else {
      return node;
    }
  }
}

Node* Relational(ASTContext& ctx) {
  auto node = Additive(ctx);

  for (;;) {
    if (auto op = ctx.t.Consume("<")) {
      node = NewNodeBinOp(Node::kGT, op, Additive(ctx), node);
    } else if (auto op = ctx.t.Consume("<=")) {
      node = NewNodeBinOp(Node::kLE, op, node, Additive(ctx));
    } else if (auto op = ctx.t.Consume(">")) {
      node = NewNodeBinOp(Node::kGT, op, node, Additive(ctx));
    } else if (auto op = ctx.t.Consume(">=")) {
      node = NewNodeBinOp(Node::kLE, op, Additive(ctx), node);
    } else {
      return node;
    }
  }
}

Node* Additive(ASTContext& ctx) {
  auto node = Multiplicative(ctx);

  for (;;) {
    if (auto op = ctx.t.Consume("+")) {
      node = NewNodeBinOp(Node::kAdd, op, node, Multiplicative(ctx));
    } else if (auto op = ctx.t.Consume("-")) {
      node = NewNodeBinOp(Node::kSub, op, node, Multiplicative(ctx));
    } else {
      return node;
    }
  }
}

Node* Multiplicative(ASTContext& ctx) {
  auto node = Unary(ctx);

  for (;;) {
    if (auto op = ctx.t.Consume("*")) {
      node = NewNodeBinOp(Node::kMul, op, node, Unary(ctx));
    } else if (auto op = ctx.t.Consume("/")) {
      node = NewNodeBinOp(Node::kDiv, op, node, Unary(ctx));
    } else {
      return node;
    }
  }
}

Node* Unary(ASTContext& ctx) {
  if (ctx.t.Consume("+")) {
    return Unary(ctx);
  } else if (auto op = ctx.t.Consume("-")) {
    auto zero = NewNodeInt(nullptr, 0);
    auto node = Unary(ctx);
    return NewNodeBinOp(Node::kSub, op, zero, node);
  } else if (auto op = ctx.t.Consume(Token::kSizeof)) {
    ctx.t.Expect("(");
    auto arg = TypeSpecifier(ctx);
    if (arg == nullptr) {
      arg = Expression(ctx);
    }
    ctx.t.Expect(")");
    return NewNodeOneChild(Node::kSizeof, op, arg);
  }

  auto cur_token = ctx.t.Peek();
  if (cur_token->raw.size() == 1) {
    if (auto it = kUnaryOps.find(cur_token->raw[0]);
        it != kUnaryOps.end() && cur_token->kind == Token::kReserved) {
      auto op = ctx.t.Consume();
      return NewNodeOneChild(it->second, op, Unary(ctx));
    }
  }

  return Postfix(ctx);
}

Node* Postfix(ASTContext& ctx) {
  auto node = Primary(ctx);

  for (;;) {
    if (auto op = ctx.t.Consume("(")) {
      node = NewNodeOneChild(Node::kCall, op, node);
      if (!ctx.t.Consume(")")) {
        node->rhs = Expression(ctx);
        for (auto cur = node->rhs; ctx.t.Consume(","); cur = cur->next) {
          cur->next = Expression(ctx);
        }
        ctx.t.Expect(")");
      }
    } else if (auto op = ctx.t.Consume("@")) {
      node = NewNodeBinOp(Node::kCast, op, node, TypeSpecifier(ctx));
      if (node->rhs == nullptr) {
        cerr << "type spec must be specified" << endl;
        ErrorAt(ctx.src, *op);
      }
    } else if (auto op = ctx.t.Consume("[")) {
      auto subscr = Expression(ctx);
      ctx.t.Expect("]");
      node = NewNodeBinOp(Node::kSubscr, op, node, subscr);
    } else {
      return node;
    }
  }
}

Node* Primary(ASTContext& ctx) {
  if (ctx.t.Consume("(")) {
    auto node = Expression(ctx);
    ctx.t.Expect(")");
    return node;
  } else if (auto id = ctx.t.Consume(Token::kId)) {
    auto node = NewNode(Node::kId, id);
    if (auto obj = ctx.sc.FindObject(id->raw)) {
      node->value = obj;
    } else {
      ctx.undeclared_ids.push_back(node);
    }
    return node;
  } else if (auto token = ctx.t.Consume(Token::kStr)) {
    return NewNodeStr(ctx, token);
  } else if (auto token = ctx.t.Consume(Token::kChar)) {
    return NewNodeChar(token);
  }
  auto token = ctx.t.Expect(Token::kInt);
  return NewNodeInt(token, get<opela_type::Int>(token->value));
}

Node* TypeSpecifier(ASTContext& ctx) {
  if (auto ptr_token = ctx.t.Consume("*")) {
    auto base_tspec = TypeSpecifier(ctx);
    if (!base_tspec) {
      cerr << "pointer base type must be specified" << endl;
      ErrorAt(ctx.src, *ptr_token);
    }
    auto node = NewNodeType(
        ptr_token, NewTypePointer(base_tspec->type));
    return node;
  }

  if (auto func_token = ctx.t.Consume(Token::kFunc)) {
    ctx.t.Expect("(");
    auto plist = ParameterDeclList(ctx);
    ctx.t.Expect(")");
    auto ret_tspec = TypeSpecifier(ctx);
    if (ret_tspec == nullptr) {
      ret_tspec = NewNodeType(nullptr, ctx.tm.Find("void"));
    }

    Type* param_type = ParamTypeFromDeclList(plist);
    auto node = NewNodeType(
        func_token, NewTypeFunc(ret_tspec->type, param_type));
    return node;
  }

  if (auto arr_token = ctx.t.Consume("[")) {
    auto arr_size = Expression(ctx);
    ctx.t.Expect("]");
    if (arr_size->kind != Node::kInt) {
      cerr << "array size must be an integer literal" << endl;
      ErrorAt(ctx.src, *arr_size->token);
    }

    auto elem_type = TypeSpecifier(ctx);
    if (elem_type == nullptr) {
      cerr << "element type must be specified" << endl;
      ErrorAt(ctx.src, *ctx.t.Peek());
    }

    auto arr_type = NewTypeArray(elem_type->type, get<long>(arr_size->value));
    return NewNodeType(arr_token, arr_type);
  }

  if (auto name_token = ctx.t.Consume(Token::kId)) {
    auto t = ctx.tm.Find(*name_token);
    if (t == nullptr) {
      t = NewTypeUnresolved(name_token);
      ctx.unresolved_types.push_back(t);
    }
    auto node = NewNodeType(name_token, t);
    return node;
  }

  return nullptr;
}

Node* ParameterDeclList(ASTContext& ctx) {
  auto head = NewNode(Node::kInt, nullptr); // dummy
  auto cur = head;

  vector<Token*> params_untyped;
  for (;;) {
    auto name_or_type = ctx.t.Consume(Token::kId);
    if (name_or_type == nullptr) {
      return head->next;
    }
    params_untyped.push_back(name_or_type);

    if (ctx.t.Consume(",")) {
      continue;
    } else if (auto tspec = TypeSpecifier(ctx)) {
      for (auto param_token : params_untyped) {
        cur->next = NewNodeOneChild(Node::kParam, param_token, tspec);
        cur = cur->next;
      }
      if (ctx.t.Consume(",")) {
        params_untyped.clear();
      } else {
        return head->next;
      }
    } else {
      for (auto tname_token : params_untyped) {
        auto tspec = NewNodeType(ctx, tname_token);
        cur->next = NewNodeOneChild(Node::kParam, nullptr, tspec);
        cur = cur->next;
      }
      params_untyped.clear();
    }
  }
}

void PrintAST(std::ostream& os, Node* ast) {
  PrintAST(os, ast, 0, false);
}

void PrintASTRec(std::ostream& os, Node* ast) {
  PrintAST(os, ast, 0, true);
}

int CountListItems(Node* head) {
  int num = 0;
  for (auto node = head; node; node = node->next) {
    ++num;
  }
  return num;
}

opela_type::String DecodeEscapeSequence(Source& src, Token& token) {
  if (token.kind != Token::kStr || token.raw[0] != '"') {
    cerr << "invalid string literal" << endl;
    ErrorAt(src, token);
  }

  opela_type::String decoded;

  for (size_t i = 1;;) {
    if (i >= token.raw.length()) {
      cerr << "incomplete string literal" << endl;
      ErrorAt(src, token);
    }
    if (token.raw[i] == '"') {
      return decoded;
    }
    if (token.raw[i] != '\\') {
      decoded.push_back(token.raw[i++]);
      continue;
    }
    decoded.push_back(GetEscapeValue(token.raw[i + 1]));
    i += 2;
  }
}

void ResolveIDs(ASTContext& ctx) {
  while (!ctx.undeclared_ids.empty()) {
    auto target = ctx.undeclared_ids.front();
    ctx.undeclared_ids.pop_front();

    auto globals = ctx.sc.GetGlobals();
    auto it = find_if(globals.begin(), globals.end(),
                      [target](auto o){
                        return o->id->raw == target->token->raw;
                      });
    if (it != globals.end()) {
      target->value = *it;
    } else {
      cerr << "undeclared id" << endl;
      ErrorAt(ctx.src, *target->token);
    }
  }
}

void ResolveType(ASTContext& ctx) {
  while (!ctx.unresolved_types.empty()) {
    auto target = ctx.unresolved_types.front();
    ctx.unresolved_types.pop_front();

    auto target_name = get<Token*>(target->value);
    auto t = ctx.tm.Find(*target_name);
    if (t == nullptr) {
      cerr << "undeclared type" << endl;
      ErrorAt(ctx.src, *target_name);
    }
    *target = *t;
  }
}

Type* MergeTypeBinOp(Type* l, Type* r) {
  l = GetUserBaseType(l);
  r = GetUserBaseType(r);
  if (IsIntegral(l) && IsIntegral(r)) {
    long l_bits = get<long>(l->value);
    long r_bits = get<long>(r->value);
    if (l_bits > r_bits) {
      return l;
    } else if (l_bits < r_bits) {
      return r;
    } else { // l_bits == r_bits
      if (l->kind == r->kind) {
        return l;
      } else if (l->kind == Type::kUInt) {
        return l;
      } else { // r->kind == Type::kUInt
        return r;
      }
    }
  } else {
    return l;
  }
}

void SetType(ASTContext& ctx, Node* node) {
  if (node == nullptr || node->type != nullptr) {
    return;
  }

  switch (node->kind) {
  case Node::kInt:
    node->type = ctx.tm.Find("int");
    break;
  case Node::kAdd:
    {
      SetType(ctx, node->lhs);
      SetType(ctx, node->rhs);
      auto l = GetUserBaseType(node->lhs->type);
      auto r = GetUserBaseType(node->rhs->type);
      if (l->kind == Type::kPointer && IsIntegral(r)) {
        node->type = node->lhs->type;
      } else {
        node->type = MergeTypeBinOp(node->lhs->type, node->rhs->type);
      }
    }
    break;
  case Node::kSub:
    {
      SetType(ctx, node->lhs);
      SetType(ctx, node->rhs);
      auto l = GetUserBaseType(node->lhs->type);
      auto r = GetUserBaseType(node->rhs->type);
      if (l->kind == Type::kPointer && r->kind == Type::kPointer) {
        node->type = ctx.tm.Find("int");
      } else if (l->kind == Type::kPointer && IsIntegral(r)) {
        node->type = node->lhs->type;
      } else {
        node->type = MergeTypeBinOp(node->lhs->type, node->rhs->type);
      }
    }
    break;
  case Node::kMul:
  case Node::kDiv:
    SetType(ctx, node->lhs);
    SetType(ctx, node->rhs);
    node->type = MergeTypeBinOp(node->lhs->type, node->rhs->type);
    break;
  case Node::kEqu:
  case Node::kNEqu:
  case Node::kGT:
  case Node::kLE:
    SetType(ctx, node->lhs);
    SetType(ctx, node->rhs);
    node->type = ctx.tm.Find("bool");
    break;
  case Node::kBlock:
    for (auto stmt = node->next; stmt; stmt = stmt->next) {
      SetType(ctx, stmt);
    }
    break;
  case Node::kId:
    {
      auto obj = get<Object*>(node->value);
      SetType(ctx, obj->def);
      node->type = obj->type;
    }
    break;
  case Node::kDefVar:
    SetType(ctx, node->rhs);
    if (node->cond) {
      node->type = node->cond->type;
    } else if (node->rhs) {
      node->type = node->rhs->type;
    }
    get<Object*>(node->lhs->value)->type = node->type;
    break;
  case Node::kDefFunc:
    get<Object*>(node->value)->type = NewTypeFunc(node->cond->type, nullptr);
    break;
  case Node::kRet:
    SetType(ctx, node->lhs);
    node->type = node->lhs->type;
    break;
  case Node::kIf:
    SetType(ctx, node->cond);
    SetType(ctx, node->lhs);
    if (node->rhs) {
      SetType(ctx, node->rhs);
    }
    break;
  case Node::kAssign:
    SetType(ctx, node->lhs);
    SetType(ctx, node->rhs);
    node->type = node->lhs->type;
    break;
  case Node::kLoop:
    SetType(ctx, node->lhs);
    break;
  case Node::kFor:
    if (node->rhs) {
      SetType(ctx, node->rhs);
    }
    SetType(ctx, node->cond);
    if (node->rhs) {
      SetType(ctx, node->rhs->next);
    }
    SetType(ctx, node->lhs);
    break;
  case Node::kCall:
    SetType(ctx, node->lhs);
    for (auto arg = node->rhs; arg; arg = arg->next) {
      SetType(ctx, arg);
    }
    if (auto t = node->lhs->type; t->kind == Type::kFunc) {
      node->type = t->base;
    } else if (t->kind == Type::kPointer && t->base->kind == Type::kFunc) {
      node->type = t->base->base;
    } else {
      cerr << "not implemented call for " << t << endl;
      ErrorAt(ctx.src, *node->token);
    }
    break;
  case Node::kStr:
    {
      auto len = ctx.strings[get<StringIndex>(node->value).i].length();
      node->type = NewTypeArray(ctx.tm.Find("uint8"), len);
    }
    break;
  case Node::kExtern:
    node->type = node->lhs->type;
    get<Object*>(node->value)->type = node->lhs->type;
    break;
  case Node::kSizeof:
    SetType(ctx, node->lhs);
    node->type = ctx.tm.Find("int");
    break;
  case Node::kCast:
    SetType(ctx, node->lhs);
    node->type = node->rhs->type;
    break;
  case Node::kParam:
    node->type = node->lhs->type;
    get<Object*>(node->value)->type = node->type;
    break;
  case Node::kType:
  case Node::kTypedef:
    break;
  case Node::kAddr:
    SetType(ctx, node->lhs);
    node->type = NewTypePointer(node->lhs->type);
    break;
  case Node::kDeref:
    SetType(ctx, node->lhs);
    if (auto t = GetUserBaseType(node->lhs->type);
        t->kind != Type::kArray && t->kind != Type::kPointer) {
      cerr << "cannot deref non-pointer type: " << t << endl;
      ErrorAt(ctx.src, *node->token);
    } else {
      node->type = t->base;
    }
    break;
  case Node::kSubscr:
    SetType(ctx, node->lhs);
    SetType(ctx, node->rhs);
    if (auto t = GetUserBaseType(node->lhs->type);
        t->kind != Type::kArray && t->kind != Type::kPointer) {
      cerr << "cannot deref non-pointer type: " << t << endl;
      ErrorAt(ctx.src, *node->token);
    } else {
      node->type = t->base;
    }
    break;
  case Node::kChar:
    node->type = ctx.tm.Find("byte");
    break;
  case Node::kLAnd:
  case Node::kLOr:
    SetType(ctx, node->lhs);
    SetType(ctx, node->rhs);
    node->type = ctx.tm.Find("bool");
    break;
  case Node::kBreak:
  case Node::kCont:
    node->type = nullptr;
    break;
  }
}

void SetTypeProgram(ASTContext& ctx, Node* ast) {
  if (ast == nullptr) {
    return;
  }

  switch (ast->kind) {
  case Node::kDefVar:
    SetType(ctx, ast);
    SetTypeProgram(ctx, ast->next);
    break;
  case Node::kDefFunc:
    for (auto param = ast->rhs; param; param = param->next) {
      SetType(ctx, param);
    }
    for (auto stmt = ast->lhs->next; stmt; stmt = stmt->next) {
      SetType(ctx, stmt);
    }
    get<Object*>(ast->value)->type =
      NewTypeFunc(ast->cond->type, ParamTypeFromDeclList(ast->rhs));
    SetTypeProgram(ctx, ast->next);
    break;
  case Node::kExtern:
  case Node::kTypedef:
    SetTypeProgram(ctx, ast->next);
    break;
  default:
    break;
  }
}
