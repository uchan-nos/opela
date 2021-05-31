#pragma once

#include <ostream>
#include <variant>
#include <vector>

#include "object.hpp"
#include "token.hpp"
#include "types.hpp"

struct Node {
  enum Kind {
    kInt,     // 整数リテラル
    kAdd,     // 2項演算子 +
    kSub,     // 2項演算子 -
    kMul,     // 2項演算子 *
    kDiv,     // 2項演算子 /
    kEqu,     // 2項演算子 ==
    kNEqu,    // 2項演算子 !=
    kGT,      // 2項演算子 >
    kLE,      // 2項演算子 <=
    kBlock,   // 複文
    kId,      // 識別子（変数、関数、型）
    kDefVar,  // 変数定義
    kDefFunc, // 関数定義
    kRet,     // return 文
    kIf,      // if 文
    kAssign,  // 2項演算子 =
    kLoop,    // 無限ループ
    kFor,     // 条件付きループ
  } kind;

  Token* token; // このノードを代表するトークン

  // 子ノード
  Node* lhs = nullptr;
  Node* rhs = nullptr;
  Node* cond = nullptr; // 条件式
  Node* next = nullptr; // 複文中での次の文


  /* 2項演算以外での lhs と rhs の用途
   *
   * kDefFunc
   *   lhs: 関数本体の複文
   * kRet
   *   lhs: 戻り値を表す式（式が無い場合は nullptr）
   * kIf
   *   lhs: then 節
   *   rhs: else 節
   * kLoop
   *   lhs: body
   * kFor
   *   lhs: body
   *   rhs: 初期化式（rhs->next が更新式）
   */

  std::variant<opela_type::Int, Object*> value = {};
  int ershov = 0;
};

struct ASTContext {
  Source& src;
  Tokenizer& t;
  Scope& sc;
  std::vector<Object*>& locals;
};

Node* Program(Source& src, Tokenizer& t);
Node* Statement(ASTContext& ctx);
Node* CompoundStatement(ASTContext& ctx);
Node* SelectionStatement(ASTContext& ctx);
Node* IterationStatement(ASTContext& ctx);
Node* ExpressionStatement(ASTContext& ctx);
Node* Expression(ASTContext& ctx);
Node* Assignment(ASTContext& ctx);
Node* Equality(ASTContext& ctx);
Node* Relational(ASTContext& ctx);
Node* Additive(ASTContext& ctx);
Node* Multiplicative(ASTContext& ctx);
Node* Unary(ASTContext& ctx);
Node* Primary(ASTContext& ctx);

void PrintAST(std::ostream& os, Node* ast);
void PrintASTRec(std::ostream& os, Node* ast);
