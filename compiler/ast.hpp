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
  } kind;

  // 線形リストの次の要素
  // kind == kFunc の場合，引数リスト
  Type* next;

  // ポインタのベース型
  Type* base;

  // 関数の戻り値型
  Type* ret;
};

struct LVar {
  Context* ctx;
  Token* token;
  std::int64_t offset;
  Type* type;
};

struct Context {
  std::string func_name;
  std::map<std::string, LVar*> local_vars;
  std::vector<LVar*> params;

  std::size_t StackSize() const { return local_vars.size() * 8; }
};

inline std::map<std::string /* func name */, Context*> contexts;
inline std::map<std::string /* id */, Node*> symbols;

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
    kLVar, // local variable
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
  } kind;

  Token* token; // このノードを代表するトークン
  Node* next; // 複文中で次の文を表す
  Node* cond; // 条件式がある文における条件式
  Node* lhs;
  Node* rhs;

  union {
    std::int64_t i;
    LVar* lvar;
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
