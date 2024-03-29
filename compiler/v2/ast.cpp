#include "ast.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <list>
#include <map>
#include <set>
#include <sstream>
#include <string>

#include "magic_enum.hpp"
#include "mangle.hpp"
#include "object.hpp"

using namespace std;
using std::filesystem::path, std::filesystem::create_directory;

namespace {

template <class T>
class Numbering {
 public:
  size_t Number(T value) {
    if (auto it = number_.find(value); it != number_.end()) {
      return it->second;
    }
    auto n = number_.size();
    number_.insert({value, n});
    return n;
  }

  const auto& GetMapping() const {
    return number_;
  }

 private:
  map<T, size_t> number_;
};

Numbering<Node*> node_number;
std::string NodeName(Node* node) {
  if (node == nullptr) {
    return "null";
  }
  ostringstream oss;
  oss << "Node_" << node_number.Number(node);
  return oss.str();
}

Numbering<Type*> type_number;
std::string TypeName(Type* type) {
  if (type == nullptr) {
    return "null";
  }
  ostringstream oss;
  oss << "Type_" << type_number.Number(type);
  return oss.str();
}

Numbering<Object*> object_number;
std::string ObjectName(Object* object) {
  if (object == nullptr) {
    return "null";
  }
  ostringstream oss;
  oss << "Object_" << object_number.Number(object);
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
  void operator()(TypedFunc* v) {
    os << Mangle(*v);
  }
  void operator()(TypedFuncMap* v) {
    os << '{';
    auto it = v->begin();
    if (it != v->end()) {
      os << it->first;
      for (++it; it != v->end(); ++it) {
        os << ' ' << it->first;
      }
    }
    os << '}';
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
  if (ctx.sc.FindCurrentBlock(*name)) {
    cerr << "local variable is redefined" << endl;
    ErrorAt(ctx.src, *name);
  }
  auto lvar = NewVar(name, def, Object::kLocal);
  ctx.cur_func->locals.push_back(lvar);
  ctx.sc.Put(*lvar->id, lvar);
  return lvar;
}

const map<char, Node::Kind> kUnaryOps{
  {'&', Node::kAddr},
  {'*', Node::kDeref},
};

struct Edge {
  std::string label, from, to;
};

bool operator<(const Edge& lhs, const Edge& rhs) {
  tuple l = tie(lhs.label, lhs.from, lhs.to);
  tuple r = tie(rhs.label, rhs.from, rhs.to);
  return l < r;
}

class DotEdgePrinter {
  std::set<Edge> printed_edges_;
  std::ostream& os_;

 public:
  DotEdgePrinter(std::ostream& os) : os_{os} {}
  bool Print(const Edge& e) {
    auto [ it, inserted ] = printed_edges_.insert(e);
    if (inserted) {
      os_ << e.from << " -> " << e.to << " [label=\"" << e.label << "\"];\n";
    }
    return inserted;
  }
};

void PrintASTDotEdge(DotEdgePrinter& dep, Type* type);
void PrintASTDotEdge(DotEdgePrinter& dep, Object* object);
void PrintASTDotEdge(DotEdgePrinter& dep, Node* ast, bool recursive);
void PrintASTDot(std::ostream& os, Type* type);
void PrintASTDot(std::ostream& os, Object* object);

void EscapeDotLabel(std::ostream& os, char c) {
  if (c == '\"') {
    os << "\\\"";
  } else if (c == '\\') {
    os << "\\\\";
  } else {
    os << c;
  }
}

void PrintTokenEscape(std::ostream& os, Token* token) {
  os << '\'';
  for (auto c : token->raw) {
    EscapeDotLabel(os, c);
  }
  os << '\'';
}

struct NodeValueDotPrinter {
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
    os << ObjectName(v);
  }
  void operator()(TypedFunc* v) {
    os << Mangle(*v) << "()";
  }
  void operator()(TypedFuncMap* v) {
    os << "TypedFuncMap_" << (void*)v;
  }
};

void PrintASTDotEdge(DotEdgePrinter& dep, Type* type) {
  if (type->base) {
    if (dep.Print({"base", TypeName(type), TypeName(type->base)})) {
      PrintASTDotEdge(dep, type->base);
    }
  }
  if (type->next) {
    if (dep.Print({"next", TypeName(type), TypeName(type->next)})) {
      PrintASTDotEdge(dep, type->next);
    }
  }
}

void PrintASTDotEdge(DotEdgePrinter& dep, Object* object) {
  dep.Print({"def", ObjectName(object), NodeName(object->def)});
  if (object->type) {
    dep.Print({"type", ObjectName(object), TypeName(object->type)});
    PrintASTDotEdge(dep, object->type);
  }
}

void PrintASTDotEdge(DotEdgePrinter& dep, Node* ast, bool recursive) {
  if (ast->type) {
    dep.Print({"type", NodeName(ast), TypeName(ast->type)});
    PrintASTDotEdge(dep, ast->type);
  }
  if (ast->lhs) {
    dep.Print({"lhs", NodeName(ast), NodeName(ast->lhs)});
    if (recursive) {
      PrintASTDotEdge(dep, ast->lhs, recursive);
    }
  }
  if (ast->rhs) {
    dep.Print({"rhs", NodeName(ast), NodeName(ast->rhs)});
    if (recursive) {
      PrintASTDotEdge(dep, ast->rhs, recursive);
    }
  }
  if (ast->cond) {
    dep.Print({"cond", NodeName(ast), NodeName(ast->cond)});
    if (recursive) {
      PrintASTDotEdge(dep, ast->cond, recursive);
    }
  }
  if (ast->next) {
    dep.Print({"next", NodeName(ast), NodeName(ast->next)});
    if (recursive) {
      PrintASTDotEdge(dep, ast->next, recursive);
    }
  }
  if (!get_if<VariantDummyType>(&ast->value)) {
    ostringstream oss;
    visit(NodeValueDotPrinter{oss}, ast->value);
    dep.Print({"value", NodeName(ast), oss.str()});

    if (auto p = get_if<Object*>(&ast->value)) {
      Object* obj = *p;
      PrintASTDotEdge(dep, obj);
    }
  }
}

void PrintASTDot(std::ostream& os, Type* type) {
  if (type == nullptr) {
    os << "null";
    return;
  }
  os << TypeName(type) << " [label=\"" << type << "\"];\n";
  return;
}

void PrintASTDot(std::ostream& os, Object* object) {
  if (object == nullptr) {
    os << "null";
    return;
  }
  os << ObjectName(object) << " [label=\""
     << magic_enum::enum_name(object->kind)
     << ' ' << object->id->raw
     << "\\n" << magic_enum::enum_name(object->linkage)
     << "\\nbp_offset=" << object->bp_offset
     << "\"];\n";
  return;
}

std::vector<const char*> parse_stack;
void PrintParseStack(std::ostream& os, ASTContext& ctx) {
  const char* loc = &ctx.t.Peek()->raw[0];
  auto line = ctx.src.GetLine(loc);
  os << line << '\n'
     << string(&*loc - line.begin(), ' ') << "^\n"
     << "----\n";

  for (const char* func_name : parse_stack) {
    os << func_name << '\n';
  }
}

std::set<Node*> generated_nodes;

std::filesystem::path AnimeFilePath(size_t timestamp, const char* filename) {
  create_directory(parse_anime_dir);
  ostringstream oss;
  oss << timestamp;
  auto anime_dir = path(parse_anime_dir) / path(oss.str());
  create_directory(anime_dir);
  return anime_dir / path(filename);
}

void GenerateAnimePage(ASTContext& ctx) {
  static size_t timestamp = 0;

  if (parse_anime_dir.empty()) {
    return;
  }

  ofstream stack_file{AnimeFilePath(timestamp, "stack.txt").native()};
  PrintParseStack(stack_file, ctx);

  ofstream ast_file{AnimeFilePath(timestamp, "ast.dot").native()};
  PrintGeneratedNodes(ast_file);

  ++timestamp;
}

struct ParseStackAnimator {
  ASTContext& ctx;

  ParseStackAnimator(ASTContext& ctx, const char* func_name) : ctx{ctx} {
    parse_stack.push_back(func_name);
    GenerateAnimePage(ctx);
  }

  ~ParseStackAnimator() {
    parse_stack.pop_back();
    GenerateAnimePage(ctx);
  }
};

#define PS(ctx) ParseStackAnimator psanimator((ctx), __func__)

} // namespace

// ノードコンストラクタ
Node* NewNode(Node::Kind kind, Token* token) {
  Node* n = new Node{kind, token, nullptr, nullptr, nullptr, nullptr};
  generated_nodes.insert(n);
  return n;
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

// パーサー
Node* Program(ASTContext& ctx) {
  PS(ctx);
  auto node = DeclarationSequence(ctx);
  ctx.t.Expect(Token::kEOF);
  return node;
}

Node* DeclarationSequence(ASTContext& ctx) {
  PS(ctx);
  Node head; // dummy
  auto cur = &head;
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
      return head.next;
    }
    while (cur->next) {
      cur = cur->next;
    }
  }
}

Node* FunctionDefinition(ASTContext& ctx) {
  PS(ctx);
  ctx.t.Expect(Token::kFunc);
  auto name = ctx.t.Expect(Token::kId);
  auto node = NewNode(Node::kDefFunc, name);

  ctx.tm.Enter();
  Node* generic_func_node = node;
  if (ctx.t.Peek("<")) {
    auto param_list = GParamList(ctx);
    generic_func_node = NewNodeOneChild(Node::kDefGFunc, name, node);
    generic_func_node->rhs = param_list;
    generic_func_node->value = new TypedFuncMap;
    for (auto param = param_list; param; param = param->next) {
      ctx.tm.Register(NewTypeGParam(param->token));
    }
  }

  auto func_obj = NewFunc(name, generic_func_node, Object::kGlobal);
  node->value = func_obj;

  ctx.t.Expect("(");
  node->rhs = ParameterDeclList(ctx);
  ctx.t.Expect(")");

  node->cond = TypeSpecifier(ctx); // 戻り値の型情報
  if (node->cond == nullptr) {
    node->cond = NewNode(Node::kType, nullptr);
    node->cond->type = ctx.tm.Find("void");
  }

  if (name->raw == "main") {
    func_obj->mangled_name = "main";
  } else {
    func_obj->mangled_name = MangleByDefNode(node);
  }

  ctx.sc.Put(func_obj->mangled_name, func_obj);

  ctx.sc.Enter();
  ASTContext func_ctx{ctx.src, ctx.t, ctx.tm, ctx.sc, ctx.strings,
                      ctx.unresolved_types, ctx.undeclared_ids,
                      ctx.typed_funcs, func_obj};

  for (auto param = node->rhs; param; param = param->next) {
    auto var = AllocateLVar(func_ctx, param->token, param);
    param->value = var;
  }

  node->lhs = CompoundStatement(func_ctx);
  ctx.sc.Leave();
  ctx.tm.Leave();
  return generic_func_node;
}

Node* ExternDeclaration(ASTContext& ctx) {
  PS(ctx);
  ctx.t.Expect(Token::kExtern);
  auto attr = ctx.t.Consume(Token::kStr);
  bool mangle = true;
  if (attr && attr->raw == R"("C")") {
    mangle = false;
  } else if (attr) {
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
  if (mangle) {
    obj->mangled_name = MangleByDefNode(node);
  } else {
    obj->mangled_name = id->raw;
  }
  ctx.sc.Put(*obj->id, obj);
  return node;
}

Node* TypeDeclaration(ASTContext& ctx) {
  PS(ctx);
  ctx.t.Expect(Token::kType);
  auto name_token = ctx.t.Expect(Token::kId);

  Node* tspec = nullptr;
  if (!ctx.t.Peek("<")) {
    tspec = TypeSpecifier(ctx);
  } else {
    Node* param_list = GParamList(ctx);

    ctx.tm.Enter();
    Type head; // dummy
    auto cur = &head;
    for (auto param = param_list; param; param = param->next) {
      cur->next = NewTypeGParam(param->token);
      ctx.tm.Register(cur->next);
      cur = cur->next;
    }
    tspec = TypeSpecifier(ctx);
    tspec = NewNodeType(param_list->token,
                        NewTypeGeneric(tspec->type, head.next));
    ctx.tm.Leave();
  }

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
  PS(ctx);
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
    if (ctx.cur_func) { // ローカル
      var = AllocateLVar(ctx, id, def_node);
    } else { // グローバル
      var = NewVar(id, def_node, Object::kGlobal);
      ctx.sc.Put(*var->id, var);
    }
    id_node->value = var;

    return def_node;
  };

  Node head; // dummy
  auto cur = &head;
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
    return head.next;
  }

  auto node = one_def();
  ctx.t.Expect(";");
  return node;
}

Node* Statement(ASTContext& ctx) {
  PS(ctx);
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
  PS(ctx);
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
  PS(ctx);
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
  PS(ctx);
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
  PS(ctx);
  auto node = Expression(ctx);
  if (auto op = ctx.t.Consume("++")) {
    node = NewNodeOneChild(Node::kInc, op, node);
  } else if (auto op = ctx.t.Consume("--")) {
    node = NewNodeOneChild(Node::kDec, op, node);
  }
  ctx.t.Expect(";");
  return node;
}

Node* Expression(ASTContext& ctx) {
  PS(ctx);
  return Assignment(ctx);
}

Node* Assignment(ASTContext& ctx) {
  PS(ctx);
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
    ctx.undeclared_ids.erase(node);
    auto def_node = NewNodeBinOp(Node::kDefVar, op, node, Assignment(ctx));

    auto lvar = AllocateLVar(ctx, node->token, def_node);
    node->value = lvar;

    node = def_node;
  }

  return node;
}

Node* LogicalOr(ASTContext& ctx) {
  PS(ctx);
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
  PS(ctx);
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
  PS(ctx);
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
  PS(ctx);
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
  PS(ctx);
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
  PS(ctx);
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
  PS(ctx);
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
  PS(ctx);
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
      if (ctx.undeclared_ids.count(node->lhs) > 0) {
        ctx.undeclared_ids[node->lhs] = node;
      }
    } else if (auto op = ctx.t.Consume("@")) {
      Node* rhs = ctx.t.Peek("<") ? TypeList(ctx) : TypeSpecifier(ctx);
      node = NewNodeBinOp(Node::kCast, op, node, rhs);
      if (node->rhs == nullptr) {
        cerr << "type spec must be specified" << endl;
        ErrorAt(ctx.src, *op);
      }
    } else if (auto op = ctx.t.Consume("[")) {
      auto subscr = Expression(ctx);
      ctx.t.Expect("]");
      node = NewNodeBinOp(Node::kSubscr, op, node, subscr);
    } else if (auto op = ctx.t.Consume(".")) {
      auto id = ctx.t.Expect(Token::kId);
      node = NewNodeBinOp(Node::kDot, op, node, NewNode(Node::kId, id));
    } else if (auto op = ctx.t.Consume("->")) {
      auto id = ctx.t.Expect(Token::kId);
      node = NewNodeBinOp(Node::kArrow, op, node, NewNode(Node::kId, id));
    } else {
      return node;
    }
  }
}

Node* Primary(ASTContext& ctx) {
  PS(ctx);
  if (ctx.t.Consume("(")) {
    auto node = Expression(ctx);
    ctx.t.Expect(")");
    return node;
  } else if (auto id = ctx.t.Consume(Token::kId)) {
    auto node = NewNode(Node::kId, id);
    if (auto obj = ctx.sc.Find(*id)) {
      node->value = obj;
    } else {
      ctx.undeclared_ids.insert({node, nullptr});
    }
    return node;
  } else if (auto token = ctx.t.Consume(Token::kStr)) {
    return NewNodeStr(ctx, token);
  } else if (auto token = ctx.t.Consume(Token::kChar)) {
    return NewNodeChar(token);
  } else if (auto op = ctx.t.Consume("{")) {
    auto node = NewNode(Node::kInitList, op);
    if (ctx.t.Consume("}")) {
      return node;
    }
    Node head; // dummy
    auto cur = &head;
    for (;;) {
      cur->next = Expression(ctx);
      cur = cur->next;
      if (!ctx.t.Consume(",")) {
        ctx.t.Expect("}");
        node->lhs = head.next;
        return node;
      }
    }
  }
  auto token = ctx.t.Expect(Token::kInt);
  return NewNodeInt(token, get<opela_type::Int>(token->value));
}

Node* TypeSpecifier(ASTContext& ctx) {
  PS(ctx);
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

    auto arr_type = NewTypeArray(elem_type->type, get<opela_type::Int>(arr_size->value));
    return NewNodeType(arr_token, arr_type);
  }

  if (auto struct_token = ctx.t.Consume(Token::kStruct)) {
    ctx.t.Expect("{");

    auto struct_t = NewType(Type::kStruct);
    auto cur = struct_t;
    while (!ctx.t.Consume("}")) {
      auto name = ctx.t.Expect(Token::kId);
      auto tspec = TypeSpecifier(ctx);
      if (tspec == nullptr) {
        cerr << "type must be specified" << endl;
        ErrorAt(ctx.src, *ctx.t.Peek());
      }
      cur->next = NewTypeParam(tspec->type, name);
      cur = cur->next;
      ctx.t.Expect(";");
    }
    return NewNodeType(struct_token, struct_t);
  }

  if (auto name_token = ctx.t.Consume(Token::kId)) {
    auto t = ctx.tm.Find(*name_token);
    if (t == nullptr) {
      t = NewTypeUnresolved(name_token);
      ctx.unresolved_types.push_back(t);
    }
    Node* type_list = nullptr;
    if (ctx.t.Peek("<")) {
      type_list = TypeList(ctx);
      auto conc_t = NewType(Type::kConcrete);
      conc_t->base = t;
      auto cur = conc_t;
      for (auto n = type_list->lhs; n; n = n->next) {
        cur->next = NewTypeParam(n->type, nullptr);
        cur = cur->next;
      }
      t = conc_t;
    }
    auto node = NewNodeType(name_token, t);
    return node;
  }

  return nullptr;
}

Node* ParameterDeclList(ASTContext& ctx) {
  PS(ctx);
  Node head; // dummy
  auto cur = &head;

  vector<Token*> params_untyped;
  for (;;) {
    if (auto op = ctx.t.Consume("...")) { // 可変長引数
      cur->next = NewNode(Node::kVParam, op);
      return head.next;
    }
    auto name_or_type = ctx.t.Consume(Token::kId);
    if (name_or_type == nullptr) {
      if (auto tspec = TypeSpecifier(ctx)) {
        cur->next = NewNodeOneChild(Node::kParam, nullptr, tspec);
        cur = cur->next;
        if (ctx.t.Consume(",")) {
          continue;
        }
      }
      return head.next;
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
        return head.next;
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

// 型リスト（例えば < int, *byte >）を読み取る
Node* TypeList(ASTContext& ctx) {
  PS(ctx);
  auto list_token = ctx.t.Expect("<");
  auto type_list = NewNode(Node::kTList, list_token);
  if (ctx.t.ConsumeOrSub(">")) {
    return type_list;
  }
  if ((type_list->lhs = TypeSpecifier(ctx)) == nullptr) {
    cerr << "type must be specified" << endl;
    ErrorAt(ctx.src, *ctx.t.Peek());
  }
  for (auto cur = type_list->lhs; !ctx.t.ConsumeOrSub(">"); cur = cur->next) {
    ctx.t.Expect(",");
    if ((cur->next = TypeSpecifier(ctx)) == nullptr) {
      cerr << "type must be specified" << endl;
      ErrorAt(ctx.src, *ctx.t.Peek());
    }
  }
  return type_list;
}

// 型パラメタのリスト（例えば < T, S >）を読み取る
// kId の列となる
Node* GParamList(ASTContext& ctx) {
  PS(ctx);
  ctx.t.Expect("<");
  auto param_list = NewNode(Node::kId, ctx.t.Expect(Token::kId));
  for (auto cur = param_list; !ctx.t.Consume(">"); cur = cur->next) {
    ctx.t.Expect(",");
    cur->next = NewNode(Node::kId, ctx.t.Expect(Token::kId));
  }
  return param_list;
}

void PrintAST(std::ostream& os, Node* ast) {
  PrintAST(os, ast, 0, false);
}

void PrintASTRec(std::ostream& os, Node* ast) {
  PrintAST(os, ast, 0, true);
}

void PrintGeneratedNodes(std::ostream& os) {
  os << "digraph AST {\n";

  DotEdgePrinter dep{os};

  for (auto node : generated_nodes) {
    os << NodeName(node) << " [label=\"" << NodeName(node) << "\\n"
       << magic_enum::enum_name(node->kind) << ' ';
    if (node->token) {
      PrintTokenEscape(os, node->token);
    } else {
      os << "null-token";
    }
    os << "\"];\n";
    cerr << "printing node edges " << node->lhs << "," << node->rhs << endl;
    PrintASTDotEdge(dep, node, false);
  }
  for (auto [ type, index ] : type_number.GetMapping()) {
    PrintASTDot(os, type);
  }
  for (auto [ obj, index ] : object_number.GetMapping()) {
    PrintASTDot(os, obj);
  }
  os << "}\n";
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
    auto it = ctx.undeclared_ids.begin();
    auto target = it->first;
    auto target_ctx = it->second;
    ctx.undeclared_ids.erase(it);

    // 基本名（Object::id）が一致するグローバルオブジェクトを候補とする
    auto globals = ctx.sc.GetGlobals();
    list<Object*> candidates;
    copy_if(globals.begin(), globals.end(), back_inserter(candidates),
            [target](auto o){
              return o->id->raw == target->token->raw;
            });
    switch (candidates.size()) {
    case 0:
      cerr << "undeclared id" << endl;
      ErrorAt(ctx.src, *target->token);
    case 1:
      target->value = candidates.front();
      break;
    default:
      if (target_ctx && target_ctx->kind == Node::kCall) {
        size_t num_args = 0;
        for (auto arg = target_ctx->rhs; arg; arg = arg->next) {
          ++num_args;
        }

        // 実引数の数（num_args）と仮引数の数が等しいものに候補を絞る
        candidates.remove_if(
          [num_args](auto o){
            size_t num_params = 0;
            for (auto param = o->def->rhs; param; param = param->next) {
              ++num_params;
            }
            return num_args != num_params;
          });

        if (candidates.size() == 1) {
          target->value = candidates.front();
          break;
        }
      }

      cerr << "ambiguous id" << endl;
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
    get<Object*>(node->lhs->value)->type = node->lhs->type = node->type;
    break;
  case Node::kDefFunc:
    {
      auto f = get<Object*>(node->value);
      f->type = NewTypeFunc(node->cond->type, ParamTypeFromDeclList(node->rhs));
    }
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

    {
      Type* param_t = nullptr;
      if (auto t = node->lhs->type; t->kind == Type::kFunc) {
        node->type = t->base;
        param_t = t->next;
      } else if (t->kind == Type::kPointer && t->base->kind == Type::kFunc) {
        node->type = t->base->base;
        param_t = t->base->next;
      } else {
        cerr << "not implemented call for " << t << endl;
        ErrorAt(ctx.src, *node->token);
      }

      auto arg = node->rhs;
      while (param_t && param_t->kind != Type::kVParam) {
        if (arg == nullptr) {
          cerr << "too few arguments" << endl;
          ErrorAt(ctx.src, *node->token);
        }
        arg = arg->next;
        param_t = param_t->next;
      }
      if (arg && param_t == nullptr) {
        cerr << "too many arguments" << endl;
        ErrorAt(ctx.src, *arg->token);
      }
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
    if (node->lhs->kind == Node::kId) {
      if (auto gfunc = get<Object*>(node->lhs->value);
          gfunc->def->kind == Node::kDefGFunc &&
          node->rhs->kind == Node::kTList) { // キャスト式 Foo@<t1, t2, ...>
        ctx.tm.Enter();
        for (auto gname = gfunc->def->rhs; gname; gname = gname->next) {
          ctx.tm.Register(NewTypeGParam(gname->token));
        }
        auto tf = NewTypedFunc(gfunc, node->rhs);
        ctx.tm.Leave();

        // 具体化して欲しい関数を表に登録する
        auto mangled_name = Mangle(*tf);
        TypedFuncMap* typed_funcs;
        if (ctx.cur_func->def->kind == Node::kDefGFunc) {
          typed_funcs = get<TypedFuncMap*>(ctx.cur_func->def->value);
        } else {
          typed_funcs = &ctx.typed_funcs;
        }
        auto [ it, inserted ] = typed_funcs->insert({mangled_name, tf});
        node->value = tf = it->second;
        node->type = ConcretizeType(*tf);
        break;
      }
    }
    node->type = node->rhs->type;
    break;
  case Node::kParam:
    node->type = node->lhs->type;
    get<Object*>(node->value)->type = node->type;
    break;
  case Node::kVParam:
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
  case Node::kInc:
  case Node::kDec:
    SetType(ctx, node->lhs);
    node->type = node->lhs->type;
    break;
  case Node::kInitList:
    for (auto elem = node->lhs; elem; elem = elem->next) {
      SetType(ctx, elem);
    }
    node->type = NewType(Type::kInitList);
    if (node->lhs) {
      auto cur = node->type;
      for (auto elem = node->lhs; elem; elem = elem->next) {
        cur->next = NewTypeParam(elem->type, nullptr);
        cur = cur->next;
      }
    }
    break;
  case Node::kDot:
    SetType(ctx, node->lhs);
    if (auto t = GetUserBaseType(node->lhs->type);
        t->kind != Type::kGParam && t->kind != Type::kStruct) {
      cerr << "lhs must be a struct: " << t << endl;
      ErrorAt(ctx.src, *node->token);
    } else {
      for (auto ft = t->next; ft; ft = ft->next) {
        if (get<Token*>(ft->value)->raw == node->rhs->token->raw) {
          node->type = ft->base;
          break;
        }
      }
    }
    break;
  case Node::kArrow:
    SetType(ctx, node->lhs);
    if (auto p = GetPrimaryType(node->lhs->type);
        p->kind != Type::kGParam && p->kind != Type::kPointer) {
      cerr << "lhs must be a pointer to a struct: " << p << endl;
      ErrorAt(ctx.src, *node->token);
    } else if (auto t = GetPrimaryType(p->base);
               t->kind != Type::kGParam && t->kind != Type::kStruct) {
      cerr << "lhs must be a pointer to a struct: " << t << endl;
      ErrorAt(ctx.src, *node->token);
    } else {
      for (auto ft = t->next; ft; ft = ft->next) {
        if (get<Token*>(ft->value)->raw == node->rhs->token->raw) {
          node->type = ft->base;
          break;
        }
      }
    }
    break;
  case Node::kDefGFunc:
    SetType(ctx, node->lhs);
    break;
  case Node::kTList:
    break;
  }

  if (node->type) {
    auto conc_t = ConcretizeType(node->type);
    if (conc_t != node->type) {
      *node->type = *conc_t;
    }
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
    ctx.cur_func = get<Object*>(ast->value);
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
    SetType(ctx, ast);
    SetTypeProgram(ctx, ast->next);
    break;
  case Node::kTypedef:
    SetTypeProgram(ctx, ast->next);
    break;
  case Node::kDefGFunc:
    SetTypeProgram(ctx, ast->lhs);
    SetTypeProgram(ctx, ast->next);
    break;
  default:
    break;
  }
}

bool IsLiteral(Node* node) {
  switch (node->kind) {
  case Node::kInt:
    return true;
  case Node::kInitList:
    for (auto elem = node->lhs; elem; elem = elem->next) {
      if (IsLiteral(elem) == false) {
        return false;
      }
    }
    return true;
  default:
    return false;
  }
}

Type* ParamTypeFromDeclList(Node* plist) {
  if (plist == nullptr) {
    return nullptr;
  }

  auto param_type = NewTypeParam(plist->lhs->type, plist->token);
  auto cur = param_type;
  plist = plist->next;
  while (plist) {
    if (plist->kind == Node::kVParam) {
      cur->next = NewType(Type::kVParam);
      break;
    }
    cur->next = NewTypeParam(plist->lhs->type, plist->token);
    cur = cur->next;
    plist = plist->next;
  }
  return param_type;
}

std::string MangleByDefNode(Node* func_def) {
  if (func_def->type) {
    return Mangle(func_def->token->raw, func_def->type);
  }
  Type* param_type = ParamTypeFromDeclList(func_def->rhs);
  Type* func_type = NewTypeFunc(func_def->cond->type, param_type);
  return Mangle(func_def->token->raw, func_type);
}
