#pragma once

#include <ostream>
#include <variant>
#include <vector>

#include "object.hpp"
#include "token.hpp"
#include "types.hpp"
#include "typespec.hpp"

struct StringIndex {
  std::size_t i;
};

struct VariantDummyType {};

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
    kCall,    // 関数呼び出し演算子 ( )
    kStr,     // 文字列リテラル
    kExtern,  // extern 宣言
    kType,    // 型指定子
    kParam,   // 関数の仮引数
  } kind;

  Token* token; // このノードを代表するトークン

  Type* type;
  /* kType: 型指定子が表す型
   * 式: その式の型
   * その他： nullptr
   *   kDefFunc: 関数の型は value (Object*) が保持
   *   kParam: 仮引数の型は lhs が保持
   */

  // 子ノード
  Node* lhs = nullptr;
  Node* rhs = nullptr;
  Node* cond = nullptr; // 条件式
  Node* next = nullptr;

  /* 2項演算以外での lhs と rhs、cond の用途
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
   * kCall
   *   lhs: 呼び出し対象の式（関数名）
   *   rhs: 引数（順に next で繋がる）
   * kExtern
   *   lhs: 型情報
   *   cond: 属性（kStr ノード。無い場合は nullptr）
   * kParam:
   *   lhs: 型情報
   */

  /* next の用途
   * （各種 expression の next の用法は上記の通り）
   *
   * kBlock: 次の文
   * kDefFunc, kExtern: 次の宣言
   * kParam: 次の仮引数
   */

  std::variant<VariantDummyType, opela_type::Int, StringIndex, Object*> value = {};
  int ershov = 0;
};

struct ASTContext {
  Source& src;
  Tokenizer& t;
  std::vector<opela_type::String>& strings;
  std::vector<Object*>& decls;
  Scope* sc;
  std::vector<Object*>* locals;
};

Node* Program(ASTContext& ctx);
Node* DeclarationSequence(ASTContext& ctx);
Node* FunctionDefinition(ASTContext& ctx);
Node* ExternDeclaration(ASTContext& ctx);
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
Node* Postfix(ASTContext& ctx);
Node* Primary(ASTContext& ctx);
Node* TypeSpecifier(ASTContext& ctx);
Node* ParameterDeclList(ASTContext& ctx);

void PrintAST(std::ostream& os, Node* ast);
void PrintASTRec(std::ostream& os, Node* ast);

// 線形リスト（next によるリスト）の要素数を返す。
// nullptr -> 0
// Node{next=nullptr} -> 1
// Node{next=Node{next=nullptr}} -> 2
int CountListItems(Node* head);

opela_type::String DecodeEscapeSequence(Source& src, Token& token);
