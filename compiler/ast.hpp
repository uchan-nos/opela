#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "token.hpp"

struct Context;
struct Node;

struct Type {
  enum Kind {
    kUndefined, // そもそも型という概念がない文法要素の場合
    kUnknown, // 未知の型
    kInt,
    kPointer,
    kFunc,
    kVoid,
    kArray,
  } kind;

  // 線形リストの次の要素
  // kind == kFunc の場合，引数リスト
  Type* next;

  // ポインタのベース型，配列の要素型
  Type* base;

  // 関数の戻り値型
  Type* ret;

  // 配列の要素数
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

// false: シンボル情報を集めるモード
// true: AST を生成するモード
inline bool generate_mode;

struct Node {
  enum Kind {
    kAdd,
    kSub,
    kMul,
    kDiv,
    kInt,
    kEqu,
    kNEqu,
    kLT,
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
  } kind;

  Token* token; // このノードを代表するトークン
  Node* next; // 複文中で次の文を表す
  Node* cond; // 条件式がある文における条件式
  Node* lhs;
  Node* rhs;

  union {
    std::int64_t i;
    Symbol* sym;
  } value;

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
Node* Equality();
Node* Relational();
Node* Additive();
Node* Multiplicative();
Node* Unary();
Node* Postfix();
Node* Primary();
Node* TypeSpecifier();
Node* ParameterDeclList();
Node* VariableDefinition();

Symbol* LookupLVar(Context* ctx, const std::string& name);
Symbol* LookupSymbol(Context* ctx, const std::string& name);
std::size_t Sizeof(Token* tk, Type* type);
