#pragma once

#include <list>
#include <ostream>
#include <variant>
#include <vector>

#include "generics.hpp"
#include "object.hpp"
#include "scope.hpp"
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
    kVParam,  // 可変長仮引数 ...
    kSizeof,  // 1項演算子 sizeof
    kTypedef, // 型宣言
    kCast,    // 2項演算子 @
    kAddr,    // 1項演算子 &
    kDeref,   // 1項演算子 *
    kSubscr,  // 添え字演算子 []
    kChar,    // 文字リテラル
    kLAnd,    // 2項演算子 &&
    kLOr,     // 2項演算子 ||
    kBreak,   // break キーワード
    kCont,    // continue キーワード
    kInc,     // 後置演算子 ++
    kDec,     // 後置演算子 --
    kInitList,// 初期値リスト {e1, e2, ...}
    kDot,     // 構造体アクセス演算子
    kArrow,   // 構造体ポインタアクセス演算子
    kDefGFunc,// ジェネリック関数の定義
    kTList,  // 型パラメタ <T1, T2, ...>
  } kind;

  Token* token; // このノードを代表するトークン

  Type* type;
  /* kType: 型指定子が表す型
   * 式: その式の型
   * kDefVar: 変数の型
   * その他： nullptr
   *   kExtern: 型は value (Object*) が保持
   *   kParam: 仮引数の型は lhs が保持
   */

  // 子ノード
  Node* lhs = nullptr;
  Node* rhs = nullptr;
  Node* cond = nullptr; // 条件式
  Node* next = nullptr;

  /* 2項演算以外での lhs と rhs、cond の用途
   *
   * kDefVar
   *   lhs: 変数名（kId ノード）
   *   rhs: 初期値を表す式
   *   cond: 型情報（kType ノード。無い場合は nullptr）
   * kDefFunc
   *   lhs: 関数本体の複文
   *   rhs: 引数リスト
   *   cond: 戻り値の型情報（kType ノード）
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
   *   lhs: 型情報（kType ノード）
   * kTypedef:
   *   lhs: 型情報（kType ノード）
   * kCast:
   *   lhs: 型変換対象の式
   *   rhs: 型情報（kType ノード）
   * kAddr, kDeref:
   *   lhs: 演算対象の式
   * kInc, kDec:
   *   lhs: 演算対象の式
   * kInitList:
   *   lhs: 先頭の式（順に next で繋がる）
   * kDefGFunc:
   *   lhs: kDefFunc ノード
   *   rhs: 型引数リスト（要素は kId ノード）
   * kTList:
   *   lhs: 型情報（kType ノード。順に next で繋がる）
   */

  /* next の用途
   * （各種 expression の next の用法は上記の通り）
   *
   * kBlock: 次の文
   * kDefFunc, kExtern: 次の宣言
   * kParam: 次の仮引数
   */

  std::variant<VariantDummyType, opela_type::Int, StringIndex, Object*,
               opela_type::Byte, ConcreteFunc*> value = {};
  int ershov = 0;
};

struct ASTContext {
  Source& src;
  Tokenizer& t;
  TypeManager& tm;
  Scope<Object>& sc;
  std::vector<opela_type::String>& strings;
  std::list<Type*>& unresolved_types;
  std::list<Node*>& undeclared_ids;
  std::map<std::string, ConcreteFunc*>& concrete_funcs;
  std::vector<Object*>* locals;
};

Node* Program(ASTContext& ctx);
Node* DeclarationSequence(ASTContext& ctx);
Node* FunctionDefinition(ASTContext& ctx);
Node* ExternDeclaration(ASTContext& ctx);
Node* TypeDeclaration(ASTContext& ctx);
Node* VariableDefinition(ASTContext& ctx);
Node* Statement(ASTContext& ctx);
Node* CompoundStatement(ASTContext& ctx);
Node* SelectionStatement(ASTContext& ctx);
Node* IterationStatement(ASTContext& ctx);
Node* ExpressionStatement(ASTContext& ctx);
Node* Expression(ASTContext& ctx);
Node* Assignment(ASTContext& ctx);
Node* LogicalOr(ASTContext& ctx);
Node* LogicalAnd(ASTContext& ctx);
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

void ResolveIDs(ASTContext& ctx);
void ResolveType(ASTContext& ctx);
Type* MergeTypeBinOp(Type* l, Type* r);
void SetType(ASTContext& ctx, Node* node);
void SetTypeProgram(ASTContext& ctx, Node* ast);
bool IsLiteral(Node* node);
