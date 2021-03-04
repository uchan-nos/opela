#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "token.hpp"

struct Context;
struct Node;

struct Type {
  enum Kind {
    kUndefined, // そもそも型という概念がない文法要素の場合
    kUnknown, // 未知の型
    kInt,
    kUInt,
    kPointer,
    kFunc,
    kVoid,
    kArray,
    kUser,
    kVParam, // 可変長引数 "..."
    kStruct,
    kParam, // 関数のパラメタ
    kField, // 構造体のフィールド
  } kind;

  // 型名、パラメタ名、フィールド名
  Token* name;

  // 線形リストの次の要素
  // kind == kFunc の場合，引数リスト
  // kind == kStruct の場合、フィールドリスト
  Type* next;

  // ポインタのベース型，配列の要素型
  // kind == kFunc の場合、戻り値型
  // kind == kParam の場合、パラメタの型
  // kind == kField の場合、フィールドの型
  Type* base;

  // kind == kArray の場合，要素数
  // kind == kInt の場合，ビット数
  std::int64_t num;
};

struct Symbol {
  enum Kind {
    kLVar, // ローカル変数
    kFunc, // 普通の（OpeLaで定義された）関数
    kEVar, // 外部変数
    kEFunc, // 外部関数
    kGVar, // グローバル変数
  } kind;

  Token* token; // シンボル名
  Type* type; // シンボルの型

  Context* ctx; // ローカル変数の場合の，定義されたコンテキスト（=関数）
  std::int64_t offset; // ローカル変数の場合の，RBP オフセット
};

struct Context {
  std::string func_name;
  std::map<std::string, Symbol*> local_vars;
  std::vector<Symbol*> params;

  std::size_t StackSize() const;
};

inline std::map<std::string /* 関数名 */, Context*> contexts;
inline std::map<std::string /* シンボル名 */, Symbol*> symbols;
inline std::vector<Node*> undeclared_id_nodes;
inline std::map<std::string /* 型名 */, Type*> types;

struct Node {
  enum Kind {
    kAdd,
    kSub,
    kMul,
    kDiv,
    kInt,
    kEqu,
    kNEqu,
    kGT,
    kLE,
    kId,
    kRet,
    kIf,
    kAssign,
    kLoop, // 無限ループ
    kFor, // 条件付きループ
    kBlock, // 複文
    kCall,
    kEList, // 式リスト
    kDeclSeq,
    kDefFunc,
    kAddr,
    kDeref,
    kDefVar,
    kType,
    kPList, // パラメータリスト
    kParam,
    kExtern,
    kSubscr, // 添え字
    kStr,
    kSizeof,
    kLOr,
    kLAnd,
    kBreak,
    kCont,
    kTypedef,
    kInc, // postfix increment
    kDec, // postfix increment
    kInitList, // initializer list
    kDot, // struct.field
    kArrow, // pointer->field
    kCompoLit, // composite literal
    kCast,
  } kind;

  Token* token; // このノードを代表するトークン
  Node* next; // 複文中で次の文を表す
  Node* cond; // 条件式がある文における条件式
  Node* lhs;
  Node* rhs;
  Node* tspec; // 型定義がある場合の型

  union {
    std::int64_t i;
    Symbol* sym;
    struct {
      char* data;
      std::size_t len;
    } str;
  } value;

  // kind == kType 以外は第2フェーズで型が付く
  Type* type;
};

Node* Program();
Node* DeclarationSequence();
Node* FunctionDefinition();
Node* ExternDeclaration();
Node* Statement();
Node* CompoundStatement();
Node* SelectionStatement();
Node* IterationStatement();
Node* JumpStatement();
Node* ExpressionStatement();
Node* Expr();
Node* Assignment();
Node* LogicalOr();
Node* LogicalAnd();
Node* Equality();
Node* Relational();
Node* Additive();
Node* Multiplicative();
Node* Cast();
Node* Unary();
Node* Postfix();
Node* Primary();
Node* TypeSpecifier(bool register_type = true);
Node* ParameterDeclList();
Node* VariableDefinition();
Node* TypeDeclaration();
Node* InitializerList();

Symbol* LookupLVar(Context* ctx, const std::string& name);
Symbol* LookupSymbol(Context* ctx, const std::string& name);
std::size_t Sizeof(Token* tk, Type* type);

// false: 変化無し，true: 変化あり
bool SetSymbolType(Node* n);

extern std::map<std::string /* 型名 */, Type*> builtin_types;
Type* FindType(Token* tk);
bool IsInteger(Type::Kind kind);
std::pair<bool, Type*> IsInteger(Type* t);
size_t CalcStackOffset(
    const std::map<std::string, Symbol*>& local_vars,
    std::function<void (Symbol* lvar, size_t offset)> f);
std::ostream& operator<<(std::ostream& os, Type* t);
bool SameType(Type* lhs, Type* rhs);
bool IsCastable(Node* int_constant, Type* cast_to);
Type* GetEssentialType(Type* user_type);
