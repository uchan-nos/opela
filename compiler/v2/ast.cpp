#include "ast.hpp"

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
  node->value = type;
  return node;
}

Node* NewNodeStr(ASTContext& ctx, Token* str) {
  auto node = NewNode(Node::kStr, str);
  node->value = StringIndex{ctx.strings.size()};
  ctx.strings.push_back(DecodeEscapeSequence(ctx.src, *str));
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

  void operator()(void*) {
    os << "none";
  }
  void operator()(opela_type::Int v) {
    os << v;
  }
  void operator()(StringIndex v) {
    os << "STR" << v.i;
  }
  void operator()(Object* v) {
    char linkage = '?';
    switch (v->linkage) {
    case Object::kLocal: linkage = 'L'; break;
    case Object::kGlobal: linkage = 'G'; break;
    case Object::kExternal: linkage = 'E'; break;
    }

    switch (v->kind) {
    case Object::kUnresolved:
      os << "Unresolved[" << v->id->raw << ']';
      break;
    case Object::kVar:
      os << linkage << "Var[" << v->id->raw << ' ' << v->type << ']';
      break;
    case Object::kFunc:
      os << linkage << "Func[" << v->id->raw << ' ' << v->type << ']';
      break;
    }
  }
  void operator()(Type* v) {
    os << v;
  }
};

void PrintAST(std::ostream& os, Node* ast, int indent, bool recursive) {
  if (ast == nullptr) {
    os << "null";
    return;
  }

  os << NodeName(ast) << "{" << magic_enum::enum_name(ast->kind) << ' ';
  if (ast->token) {
    os << "'" << ast->token->raw << "'";
  } else {
    os << "null-token";
  }

  if (ast->kind == Node::kInt) {
    os << " value=" << get<opela_type::Int>(ast->value);
  }

  const bool multiline = recursive && (
      ast->lhs || ast->rhs || ast->cond || ast->next);
  if (multiline) {
    os << '\n' << string(indent + 2, ' ') << "lhs=";
    PrintAST(os, ast->lhs, indent + 2, recursive);
    os << '\n' << string(indent + 2, ' ') << "rhs=";
    PrintAST(os, ast->rhs, indent + 2, recursive);
    os << '\n' << string(indent + 2, ' ') << "cond=";
    PrintAST(os, ast->cond, indent + 2, recursive);
    os << '\n' << string(indent + 2, ' ') << "next=";
    PrintAST(os, ast->next, indent + 2, recursive);
    os << '\n' << string(indent + 2, ' ') << "value=";
    visit(NodeValuePrinter{os}, ast->value);
  } else {
    os << " lhs=" << NodeName(ast->lhs) << " rhs=" << NodeName(ast->rhs)
       << " cond=" << NodeName(ast->cond) << " next=" << NodeName(ast->next)
       << " value=";
    visit(NodeValuePrinter{os}, ast->value);
  }

  if (multiline) {
    os << '\n' << string(indent, ' ') << '}';
  } else {
    os << '}';
  }
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

  ctx.t.Expect("(");
  ctx.t.Expect(")");

  auto func_type = NewTypeFunc(builtin_types["void"], nullptr);
  auto func = NewFunc(name, func_type, Object::kGlobal);
  Scope sc;
  sc.Enter();
  ASTContext func_ctx{ctx.src, ctx.t, ctx.strings, ctx.decls,
                      &sc, &func->locals};

  auto node = NewNode(Node::kDefFunc, name);
  node->lhs = CompoundStatement(func_ctx);
  node->value = func;
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
  auto obj = NewFunc(id, get<Type*>(tspec->value), Object::kExternal);
  node->value = obj;
  ctx.decls.push_back(obj);
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

  return ExpressionStatement(ctx);
}

Node* CompoundStatement(ASTContext& ctx) {
  ctx.sc->Enter();

  auto node = NewNode(Node::kBlock, ctx.t.Expect("{"));
  auto cur = node;
  while (!ctx.t.Consume("}")) {
    cur->next = Statement(ctx);
    while (cur->next) {
      cur = cur->next;
    }
  }

  ctx.sc->Leave();
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
  auto node = Equality(ctx);

  if (auto op = ctx.t.Consume("=")) {
    node = NewNodeBinOp(Node::kAssign, op, node, Assignment(ctx));
  } else if (auto op = ctx.t.Consume(":=")) {
    if (node->kind != Node::kId) {
      cerr << "lhs of ':=' must be an identifier" << endl;
      ctx.t.Unexpected(*node->token);
    }

    if (ctx.sc->FindObjectCurrentBlock(node->token->raw)) {
      cerr << "local variable is redefined" << endl;
      ErrorAt(ctx.src, *node->token);
    }

    auto lvar = NewLVar(node->token, builtin_types["int"]);
    node->value = lvar;
    ctx.locals->push_back(lvar);
    ctx.sc->PutObject(lvar);

    node = NewNodeBinOp(Node::kDefVar, op, node, Assignment(ctx));
  }

  return node;
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
    if (auto obj = ctx.sc->FindObject(id->raw)) {
      node->value = obj;
    }
    return node;
  } else if (auto token = ctx.t.Consume(Token::kStr)) {
    return NewNodeStr(ctx, token);
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
        ptr_token, NewTypePointer(get<Type*>(base_tspec->value)));
    return node;
  }

  if (auto func_token = ctx.t.Consume(Token::kFunc)) {
    ctx.t.Expect("(");
    auto plist = ParameterDeclList(ctx);
    ctx.t.Expect(")");
    auto ret_tspec = TypeSpecifier(ctx);
    if (ret_tspec == nullptr) {
      ret_tspec = NewNodeType(nullptr, builtin_types["void"]);
    }

    Type* param_type = nullptr;
    if (plist) {
      param_type = NewTypeParam(get<Type*>(plist->value));
      plist = plist->next;
      while (plist) {
        param_type->next = NewTypeParam(get<Type*>(plist->value));
        param_type = param_type->next;
        plist = plist->next;
      }
    }
    auto node = NewNodeType(
        func_token, NewTypeFunc(get<Type*>(ret_tspec->value), param_type));
    return node;
  }

  if (auto name_token = ctx.t.Consume(Token::kId)) {
    auto t = FindType(ctx.src, *name_token);
    if (t == nullptr) {
      t = NewTypeUnresolved();
      cerr << "not implemented: unresolved type handling" << endl;
      ErrorAt(ctx.src, *name_token);
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
        cur->next = NewNode(Node::kParam, param_token);
        cur->next->value = get<Type*>(tspec->value);
        // TODO Node に Node* tspec というフィールドを足すか？
        cur = cur->next;
      }
      params_untyped.clear();
    } else {
      for (auto tname_token : params_untyped) {
        cur->next = NewNode(Node::kParam, nullptr);
        cur->value = FindType(ctx.src, *tname_token);
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
