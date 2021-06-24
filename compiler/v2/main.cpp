#include <algorithm>
#include <array>
#include <bit>
#include <bitset>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include "asm.hpp"
#include "ast.hpp"
#include "magic_enum.hpp"
#include "object.hpp"
#include "source.hpp"
#include "token.hpp"

using namespace std;

namespace {

int verbosity = 0;
string target_arch = "x86_64";

int ParseArgs(int argc, char** argv) {
  int i = 1;
  while (i < argc) {
    string_view opt = argv[i];
    if (opt == "-target-arch") {
      if (i == argc - 1) {
        cerr << "-target-arch needs one argument" << endl;
        return 1;
      }
      target_arch = argv[i + 1];
      i += 2;
    } else if (opt == "-v") {
      ++verbosity;
      ++i;
    } else {
      cerr << "unknown argument: " << opt << endl;
      return 1;
    }
  }
  return 0;
}

} // namespace

Asm::Register UseAnyCalcReg(Asm::RegSet& free_calc_regs) {
  auto reg_index = countr_zero(free_calc_regs.to_ulong());
  if (reg_index >= Asm::kRegNum) {
    return Asm::kRegNum;
  }
  free_calc_regs.reset(reg_index);
  return static_cast<Asm::Register>(reg_index);
}

bool HasChildren(Node* node) {
  if (node == nullptr) {
    return false;
  } else if (node->kind == Node::kInt) {
    return false;
  }
  return true;
}

int SetErshovNumber(Source& src, Node* expr) {
  if (expr->ershov > 0) {
    return expr->ershov;
  } else if (expr->kind == Node::kCall) {
    for (auto arg = expr->rhs; arg; arg = arg->next) {
      SetErshovNumber(src, arg);
    }
    return expr->ershov = 9;
  } else if (expr->lhs == nullptr && expr->rhs == nullptr) {
    return expr->ershov = 1;
  } else if (expr->lhs != nullptr && expr->rhs != nullptr) {
    int l = SetErshovNumber(src, expr->lhs);
    int r = SetErshovNumber(src, expr->rhs);
    return expr->ershov = l == r ? l + 1 : max(l, r);
  } else if (expr->lhs != nullptr && expr->rhs == nullptr) {
    return expr->ershov = SetErshovNumber(src, expr->lhs);
  }
  cerr << "unexpected node" << endl;
  ErrorAt(src, *expr->token);
}

struct GenContext {
  Source& src;
  Asm& asmgen;
  Object* func;
};

struct LabelSet {
  string cont, brk;
};

size_t label_counter = 0;
string GenerateLabel() {
  ostringstream oss;
  oss << "LABEL" << label_counter++;
  return oss.str();
}

string StringLabel(size_t index) {
  ostringstream oss;
  oss << "STR" << index;
  return oss.str();
}

const std::array<const char*, 9> kSizeMap{
  nullptr,
  ".byte",
  ".2byte",
  nullptr,
  ".4byte",
  nullptr,
  nullptr,
  nullptr,
  ".8byte",
};

uint64_t GenMaskBits(int bit_width) {
  if (bit_width == 64) {
    return 0xffffffffffffffff;
  }
  return (uint64_t(1) << bit_width) - 1;
}

int CeilBitsToRegSize(int bits) {
  return ((bits + 7) >> 3) << 3;
}

bool GenCast(GenContext& ctx, Asm::Register dest,
             Type* from_type, Type* to_type, bool explicit_cast = false) {
  auto f = GetUserBaseType(from_type);
  auto t = GetUserBaseType(to_type);
  if (IsEqual(f, t)) {
    return false;
  }

  if (IsIntegral(f)) {
    if (IsIntegral(t)) {
      auto f_bits = get<long>(f->value);
      auto t_bits = get<long>(t->value);
      if (t_bits < f_bits) {
        ctx.asmgen.ShiftL64(dest, 64 - t_bits);
        ctx.asmgen.ShiftR64(dest, 64 - t_bits);
      } else if (f_bits < t_bits) {
        ctx.asmgen.ShiftL64(dest, 64 - f_bits);
        if (f->kind == Type::kInt) { // sign extend
          ctx.asmgen.ShiftAR64(dest, 64 - f_bits);
        } else { // zero extend
          ctx.asmgen.ShiftR64(dest, 64 - f_bits);
        }
      }
    } else if (t->kind == Type::kBool) {
      ctx.asmgen.Set1IfNonZero64(dest, dest);
    } else if (explicit_cast && t->kind == Type::kPointer) {
      // pass
    } else {
      return true;
    }
  } else if (f->kind == Type::kBool) {
    if (!IsIntegral(t) && t->kind != Type::kBool) {
      return true;
    }
  } else if (explicit_cast && f->kind == Type::kPointer) {
    if (t->kind == Type::kPointer) {
      // pass
    } else if (IsIntegral(t)) {
      if (auto bits = get<long>(t->value); bits < 64) {
        ctx.asmgen.And64(dest, (1 << get<long>(t->value)) - 1);
      }
    } else {
      return true;
    }
  } else {
    return true;
  }
  return false;
}

Asm::DataType BytesToDataType(int bytes) {
  if (bytes == 1) {
    return Asm::kByte;
  } else if (bytes == 2) {
    return Asm::kWord;
  } else if (bytes <= 4) {
    return Asm::kDWord;
  } else if (bytes <= 8) {
    return Asm::kQWord;
  }
  return Asm::kNonStandardDataType;
}

Asm::DataType DataTypeOf(GenContext& ctx, Type* type) {
  return BytesToDataType(SizeofType(ctx.src, type));
}

Asm::DataType DataTypeOf(GenContext& ctx, Node* node) {
  auto dt = DataTypeOf(ctx, node->type);
  if (dt == Asm::kNonStandardDataType) {
    cerr << "non-standard data type: " << node->type << endl;
    ErrorAt(ctx.src, *node->token);
  }
  return dt;
}

struct EvalBinOp {
  Node* node;
  Asm::Register dest_reg, calc_reg;
  Asm::Register lhs_reg, rhs_reg;
  bool lhs_in_dest;
};

void GenerateAsm(GenContext& ctx, Node* node,
                 Asm::Register dest, Asm::RegSet free_calc_regs,
                 const LabelSet& labels, bool lval = false);

void GenerateAssign(GenContext& ctx, const EvalBinOp& e,
                    Asm::RegSet free_calc_regs, bool lval) {
  const auto lhs_t = GetUserBaseType(e.node->lhs->type);
  const auto rhs_t = GetUserBaseType(e.node->rhs->type);
  if (rhs_t->kind == Type::kInitList) {
    if (lhs_t->kind == Type::kArray) {
      const auto reg = UseAnyCalcReg(free_calc_regs);
      const auto elem_size = SizeofType(ctx.src, lhs_t->base);
      const auto elem_dt = BytesToDataType(elem_size);
      int elem_offset = 0;
      int sp_offset = 0;
      auto init_elem = e.node->rhs->lhs;
      for (int i = 0; i < get<long>(lhs_t->value); ++i) {
        if (init_elem) {
          ctx.asmgen.LoadN(reg, e.rhs_reg, sp_offset,
                           DataTypeOf(ctx, init_elem));
          ctx.asmgen.StoreN(e.lhs_reg, elem_offset, reg, elem_dt);
          init_elem = init_elem->next;
        } else {
          ctx.asmgen.StoreN(e.lhs_reg, elem_offset, Asm::kRegZero, elem_dt);
        }
        sp_offset += 8;
        elem_offset += elem_size;
      }
    } else if (lhs_t->kind == Type::kStruct) {
      const auto reg = UseAnyCalcReg(free_calc_regs);
      int field_offset = 0;
      int sp_offset = 0;
      auto init_elem = e.node->rhs->lhs;
      for (auto ft = lhs_t->next; ft; ft = ft->next) {
        const auto field_size = SizeofType(ctx.src, ft);
        const auto field_dt = BytesToDataType(field_size);
        if (init_elem) {
          ctx.asmgen.LoadN(reg, e.rhs_reg, sp_offset,
                           DataTypeOf(ctx, init_elem));
          ctx.asmgen.StoreN(e.lhs_reg, field_offset, reg, field_dt);
          init_elem = init_elem->next;
        } else {
          ctx.asmgen.StoreN(e.lhs_reg, field_offset, Asm::kRegZero, field_dt);
        }
        sp_offset += 8;
        field_offset += field_size;
      }
    }
  } else {
    ctx.asmgen.StoreN(e.lhs_reg, 0, e.rhs_reg, DataTypeOf(ctx, e.node->lhs));
  }
  if (lval && !e.lhs_in_dest) {
    ctx.asmgen.Mov64(e.dest_reg, e.calc_reg);
  } else if (!lval && e.lhs_in_dest) {
    ctx.asmgen.Mov64(e.dest_reg, e.calc_reg);
  }
}

void GenerateGVarData(GenContext& ctx, Type* obj_t, Node* init) {
  obj_t = GetUserBaseType(obj_t);
  const auto obj_size = SizeofType(ctx.src, obj_t);

  if (init == nullptr || IsLiteral(init) == false) {
    ctx.asmgen.Output() << "    .zero " << obj_size << '\n';
  } else if (init->kind == Node::kInt) {
    ctx.asmgen.Output() << "    " << kSizeMap[obj_size] << ' '
                        << get<opela_type::Int>(init->value) << '\n';
  } else if (init->kind == Node::kInitList && obj_t->kind == Type::kArray) {
    auto init_elem = init->lhs;
    for (int i = 0; i < get<long>(obj_t->value); ++i) {
      GenerateGVarData(ctx, obj_t->base, init_elem);
      init_elem = init_elem ? init_elem->next : nullptr;
    }
  } else if (init->kind == Node::kInitList && obj_t->kind == Type::kStruct) {
    auto init_elem = init->lhs;
    for (auto ft = obj_t->next; ft; ft = ft->next) {
      GenerateGVarData(ctx, ft->base, init_elem);
      init_elem = init_elem ? init_elem->next : nullptr;
    }
  } else {
    cerr << "unknown initial data type" << endl;
    ErrorAt(ctx.src, *init->token);
  }
}

void GenerateAsm(GenContext& ctx, Node* node,
                 Asm::Register dest, Asm::RegSet free_calc_regs,
                 const LabelSet& labels, bool lval) {
  auto comment_node = [ctx, node]{
    ctx.asmgen.Output() << "    # ";
    PrintAST(ctx.asmgen.Output(), node);
    ctx.asmgen.Output() << '\n';
  };

  switch (node->kind) {
  case Node::kInt:
    comment_node();
    ctx.asmgen.Mov64(dest, get<opela_type::Int>(node->value));
    return;
  case Node::kBlock:
    for (auto stmt = node->next; stmt; stmt = stmt->next) {
      GenerateAsm(ctx, stmt, dest, free_calc_regs, labels);
    }
    return;
  case Node::kId:
    comment_node();
    if (auto p = get_if<Object*>(&node->value)) {
      Object* obj = *p;
      switch (obj->linkage) {
      case Object::kLocal:
        if (lval) {
          ctx.asmgen.LEA(dest, Asm::kRegBP, obj->bp_offset);
        } else {
          ctx.asmgen.LoadN(dest, Asm::kRegBP, obj->bp_offset,
                           DataTypeOf(ctx, obj->type));
        }
        break;
      case Object::kGlobal:
      case Object::kExternal:
        if (lval || obj->kind == Object::kFunc) {
          ctx.asmgen.LoadLabelAddr(dest, obj->id->raw);
        } else {
          ctx.asmgen.LoadN(dest, obj->id->raw, DataTypeOf(ctx, obj->type));
        }
        break;
      }
    }
    return;
  case Node::kDefVar:
    if (node->rhs) {
      break;
    }
    return;
  case Node::kDefFunc:
    {
      auto func = get<Object*>(node->value);
      GenContext func_ctx{ctx.src, ctx.asmgen, func};

      int stack_size = 0;
      for (Object* obj : func->locals) {
        stack_size += (SizeofType(ctx.src, obj->type) + 7) & ~7;
        obj->bp_offset = -stack_size;
      }
      stack_size = (stack_size + 0xf) & ~static_cast<size_t>(0xf);

      ctx.asmgen.Output() << ".global " << func->id->raw << '\n'
                          << func->id->raw << ":\n";
      ctx.asmgen.Push64(Asm::kRegBP);
      ctx.asmgen.Mov64(Asm::kRegBP, Asm::kRegSP);
      ctx.asmgen.Sub64(Asm::kRegSP, stack_size);
      int arg_index = 0;
      for (auto param = node->rhs; param; param = param->next) {
        auto arg_reg = static_cast<Asm::Register>(Asm::kRegV0 + arg_index);
        ctx.asmgen.StoreN(Asm::kRegBP, -8 * (1 + arg_index),
                          arg_reg, Asm::kQWord);
        ++arg_index;
      }
      GenerateAsm(func_ctx, node->lhs, dest, free_calc_regs, labels);
      ctx.asmgen.Xor64(Asm::kRegA, Asm::kRegA);
      ctx.asmgen.Output() << func->id->raw << ".exit:\n";
      ctx.asmgen.Leave();
      ctx.asmgen.Ret();
      return;
    }
  case Node::kRet:
    comment_node();
    if (node->lhs) {
      GenerateAsm(ctx, node->lhs, dest, free_calc_regs, labels);
      if (GenCast(ctx, dest, node->lhs->type, ctx.func->type->base)) {
        cerr << "not implemented cast from " << node->lhs->type
             << " to " << ctx.func->type->base << endl;
        ErrorAt(ctx.src, *node->token);
      }
    }
    ctx.asmgen.Jmp(string{ctx.func->id->raw} + ".exit");
    return;
  case Node::kIf:
    comment_node();
    {
      auto label_exit = GenerateLabel();
      auto label_else = node->rhs ? GenerateLabel() : label_exit;
      GenerateAsm(ctx, node->cond, dest, free_calc_regs, labels);
      ctx.asmgen.JmpIfZero(dest, label_else);
      GenerateAsm(ctx, node->lhs, dest, free_calc_regs, labels);
      if (node->rhs) {
        ctx.asmgen.Jmp(label_exit);
        ctx.asmgen.Output() << label_else << ": # else clause\n";
        GenerateAsm(ctx, node->rhs, dest, free_calc_regs, labels);
      }
      ctx.asmgen.Output() << label_exit << ": # if stmt exit\n";
    }
    return;
  case Node::kLoop:
    comment_node();
    {
      LabelSet ls{GenerateLabel(), GenerateLabel()};
      ctx.asmgen.Output() << ls.cont << ": # loop body\n";
      GenerateAsm(ctx, node->lhs, dest, free_calc_regs, ls);
      ctx.asmgen.Jmp(ls.cont);
      ctx.asmgen.Output() << ls.brk << ": # loop end\n";
    }
    return;
  case Node::kFor:
    comment_node();
    {
      auto label_loop = GenerateLabel();
      auto label_cond = GenerateLabel();
      LabelSet ls{node->rhs ? GenerateLabel() : label_cond, GenerateLabel()};
      if (node->rhs) {
        GenerateAsm(ctx, node->rhs, dest, free_calc_regs, ls);
      }
      ctx.asmgen.Jmp(label_cond);
      ctx.asmgen.Output() << label_loop << ": # loop body\n";
      GenerateAsm(ctx, node->lhs, dest, free_calc_regs, ls);
      if (node->rhs) {
        ctx.asmgen.Output() << ls.cont << ": # update\n";
        GenerateAsm(ctx, node->rhs->next, dest, free_calc_regs, ls);
      }
      ctx.asmgen.Output() << label_cond << ": # condition\n";
      GenerateAsm(ctx, node->cond, dest, free_calc_regs, ls);
      ctx.asmgen.JmpIfNotZero(dest, label_loop);
      ctx.asmgen.Output() << ls.brk << ": # loop end\n";
    }
    return;
  case Node::kCall:
    {
      SetErshovNumber(ctx.src, node);
      const int num_arg = CountListItems(node->rhs);

      vector<Asm::Register> saved_regs;
      auto save_reg = [&](Asm::Register reg) {
        ctx.asmgen.Push64(reg);
        saved_regs.push_back(reg);
      };

      // 戻り値、引数レジスタを退避
      if (dest != Asm::kRegA) {
        save_reg(Asm::kRegA);
      }
      for (int i = 0; i < num_arg; ++i) {
        auto reg = static_cast<Asm::Register>(Asm::kRegV0 + i);
        if (reg != dest && !free_calc_regs.test(reg)) {
          save_reg(reg);
          free_calc_regs.set(reg);
        }
      }

      // 関数名の評価結果を格納するレジスタを探す
      Asm::Register lhs_reg = Asm::kRegNV0;
      for (int i = Asm::kRegV0 + num_arg; i <= Asm::kRegY; ++i) {
        if (free_calc_regs.test(i)) {
          lhs_reg = static_cast<Asm::Register>(i);
          break;
        }
      }
      if (lhs_reg > Asm::kRegY) {
        lhs_reg = Asm::kRegY;
        save_reg(lhs_reg);
        free_calc_regs.set(lhs_reg);
      }

      // Ershov 数が 2 以上の引数を事前に評価し、スタックに保存しておく
      vector<Node*> args;
      for (auto arg = node->rhs; arg; arg = arg->next) {
        args.push_back(arg);
        if (arg->ershov >= 2) {
          GenerateAsm(ctx, arg, dest, free_calc_regs, labels);
          ctx.asmgen.Push64(dest);
        }
      }

      GenerateAsm(ctx, node->lhs, lhs_reg, free_calc_regs, labels);
      free_calc_regs.reset(lhs_reg);

      // 引数レジスタに実引数を設定する
      for (int i = num_arg - 1; i >= 0; --i) {
        auto arg_reg = static_cast<Asm::Register>(Asm::kRegV0 + i);
        if (args[i]->ershov == 1) {
          GenerateAsm(ctx, args[i], arg_reg, free_calc_regs, labels);
        } else {
          ctx.asmgen.Pop64(arg_reg);
        }
      }

      // 関数を呼び、結果を dest レジスタにコピーする
      ctx.asmgen.Output() << "    # calling " << node->lhs->token->raw << '\n';
      ctx.asmgen.Call(lhs_reg);
      if (Asm::kRegA != dest) {
        ctx.asmgen.Mov64(dest, Asm::kRegA);
      }

      // 退避したレジスタの復帰
      while (!saved_regs.empty()) {
        ctx.asmgen.Pop64(saved_regs.back());
        saved_regs.pop_back();
      }
    }
    return;
  case Node::kStr:
    comment_node();
    ctx.asmgen.LoadLabelAddr(
        dest, StringLabel(get<StringIndex>(node->value).i));
    return;
  case Node::kExtern:
  case Node::kTypedef:
    return;
  case Node::kSizeof:
    comment_node();
    ctx.asmgen.Mov64(dest, SizeofType(ctx.src, node->lhs->type));
    return;
  case Node::kCast:
    GenerateAsm(ctx, node->lhs, dest, free_calc_regs, labels, lval);
    if (GenCast(ctx, dest, node->lhs->type, node->rhs->type, true)) {
      cerr << "not implemented cast from " << node->lhs->type
           << " to " << node->rhs->type << endl;
      ErrorAt(ctx.src, *node->token);
    }
    return;
  case Node::kChar:
    comment_node();
    ctx.asmgen.Mov64(dest, get<opela_type::Byte>(node->value));
    return;
  case Node::kLAnd:
    comment_node();
    {
      auto label_end = GenerateLabel();
      GenerateAsm(ctx, node->lhs, dest, free_calc_regs, labels);
      ctx.asmgen.JmpIfZero(dest, label_end);
      GenerateAsm(ctx, node->rhs, dest, free_calc_regs, labels);
      ctx.asmgen.Set1IfNonZero64(dest, dest);
      ctx.asmgen.Output() << label_end << ": # end of '&&'\n";
    }
    return;
  case Node::kLOr:
    comment_node();
    {
      auto label_end = GenerateLabel();
      GenerateAsm(ctx, node->lhs, dest, free_calc_regs, labels);
      ctx.asmgen.JmpIfNotZero(dest, label_end);
      GenerateAsm(ctx, node->rhs, dest, free_calc_regs, labels);
      ctx.asmgen.Output() << label_end << ": # end of '||'\n";
      ctx.asmgen.Set1IfNonZero64(dest, dest);
    }
    return;
  case Node::kBreak:
    comment_node();
    ctx.asmgen.Jmp(labels.brk);
    return;
  case Node::kCont:
    comment_node();
    ctx.asmgen.Jmp(labels.cont);
    return;
  case Node::kInc:
    comment_node();
    GenerateAsm(ctx, node->lhs, dest, free_calc_regs, labels, true);
    ctx.asmgen.IncN(dest, DataTypeOf(ctx, node));
    return;
  case Node::kDec:
    comment_node();
    GenerateAsm(ctx, node->lhs, dest, free_calc_regs, labels, true);
    ctx.asmgen.DecN(dest, DataTypeOf(ctx, node));
    return;
  case Node::kInitList:
    comment_node();
    {
      int sp_offset = 0;
      for (auto elem = node->lhs; elem; elem = elem->next) {
        auto esize = SizeofType(ctx.src, elem->type);
        sp_offset += (esize + 7) & ~7;
      }
      ctx.asmgen.Sub64(Asm::kRegSP, (sp_offset + 0xf) & ~0xf);
      sp_offset = 0;
      for (auto elem = node->lhs; elem; elem = elem->next) {
        GenerateAsm(ctx, elem, dest, free_calc_regs, labels);
        ctx.asmgen.StoreN(Asm::kRegSP, sp_offset, dest, DataTypeOf(ctx, elem));
        auto esize = SizeofType(ctx.src, elem->type);
        sp_offset += (esize + 7) & ~7;
      }
      ctx.asmgen.Mov64(dest, Asm::kRegSP);
    }
    return;
  case Node::kDot:
    {
      size_t field_offset = 0;
      auto ft = GetUserBaseType(node->lhs->type)->next;
      for (; ft; ft = ft->next) {
        if (get<Token*>(ft->value)->raw == node->rhs->token->raw) {
          break;
        }
        field_offset += SizeofType(ctx.src, ft);
      }
      GenerateAsm(ctx, node->lhs, dest, free_calc_regs, labels, true);
      if (lval) {
        ctx.asmgen.Add64(dest, field_offset);
      } else {
        ctx.asmgen.LoadN(dest, dest, field_offset, DataTypeOf(ctx, ft->base));
      }
    }
    return;
  case Node::kArrow:
    {
      size_t field_offset = 0;
      auto ptr_t = GetUserBaseType(node->lhs->type);
      auto ft = GetUserBaseType(ptr_t->base)->next;
      for (; ft; ft = ft->next) {
        if (get<Token*>(ft->value)->raw == node->rhs->token->raw) {
          break;
        }
        field_offset += SizeofType(ctx.src, ft);
      }
      GenerateAsm(ctx, node->lhs, dest, free_calc_regs, labels, false);
      if (lval) {
        ctx.asmgen.Add64(dest, field_offset);
      } else {
        ctx.asmgen.LoadN(dest, dest, field_offset, DataTypeOf(ctx, ft->base));
      }
    }
    return;
  default:
    ; // pass
  }

  // ここから Expression に対する処理
  SetErshovNumber(ctx.src, node);

  const bool request_lval =
    node->kind == Node::kAssign ||
    node->kind == Node::kDefVar ||
    node->kind == Node::kAddr ||
    (node->kind == Node::kSubscr && node->lhs->type->kind != Type::kPointer)
    ;

  Asm::Register reg;
  const bool lhs_in_dest = node->rhs == nullptr ||
                           node->lhs->ershov >= node->rhs->ershov;
  if (lhs_in_dest) {
    GenerateAsm(ctx, node->lhs, dest, free_calc_regs, labels, request_lval);
    if (node->rhs) {
      reg = UseAnyCalcReg(free_calc_regs);
      GenerateAsm(ctx, node->rhs, reg, free_calc_regs, labels);
    }
  } else {
    GenerateAsm(ctx, node->rhs, dest, free_calc_regs, labels);
    reg = UseAnyCalcReg(free_calc_regs);
    GenerateAsm(ctx, node->lhs, reg, free_calc_regs, labels, request_lval);
  }
  auto lhs_reg = lhs_in_dest ? dest : reg;
  auto rhs_reg = lhs_in_dest ? reg : dest;

  auto lhs_t = GetUserBaseType(node->lhs->type);
  auto rhs_t = node->rhs ? GetUserBaseType(node->rhs->type) : nullptr;

  comment_node();

  switch (node->kind) {
  case Node::kAdd:
    if (IsIntegral(lhs_t) && IsIntegral(rhs_t)) {
      ctx.asmgen.Add64(dest, reg);
    } else if (lhs_t->kind == Type::kPointer && IsIntegral(rhs_t)) {
      ctx.asmgen.Mul64(rhs_reg, rhs_reg, SizeofType(ctx.src, lhs_t->base));
      ctx.asmgen.Add64(dest, reg);
    } else if (IsIntegral(lhs_t) && rhs_t->kind == Type::kPointer) {
      ctx.asmgen.Mul64(lhs_reg, lhs_reg, SizeofType(ctx.src, rhs_t->base));
      ctx.asmgen.Add64(dest, reg);
    } else {
      cerr << "not supported " << lhs_t << " + " << rhs_t << endl;
      ErrorAt(ctx.src, *node->token);
    }
    break;
  case Node::kSub:
    if (IsIntegral(lhs_t) && IsIntegral(rhs_t)) {
      ctx.asmgen.Sub64(lhs_reg, rhs_reg);
    } else if (lhs_t->kind == Type::kPointer && IsIntegral(rhs_t)) {
      ctx.asmgen.Mul64(rhs_reg, rhs_reg, SizeofType(ctx.src, lhs_t->base));
      ctx.asmgen.Sub64(lhs_reg, rhs_reg);
    } else if (IsIntegral(lhs_t) && rhs_t->kind == Type::kPointer) {
      ctx.asmgen.Mul64(lhs_reg, lhs_reg, SizeofType(ctx.src, rhs_t->base));
      ctx.asmgen.Sub64(lhs_reg, rhs_reg);
    } else if (IsEqual(lhs_t, rhs_t)) {
      ctx.asmgen.Sub64(lhs_reg, rhs_reg);
      auto tmp_reg = UseAnyCalcReg(free_calc_regs);
      ctx.asmgen.Mov64(tmp_reg, SizeofType(ctx.src, lhs_t->base));
      ctx.asmgen.Div64(lhs_reg, tmp_reg);
    } else {
      cerr << "not supported " << lhs_t << " - " << rhs_t << endl;
      ErrorAt(ctx.src, *node->token);
    }
    if (!lhs_in_dest) {
      ctx.asmgen.Mov64(dest, reg);
    }
    break;
  case Node::kMul:
    ctx.asmgen.Mul64(dest, reg);
    break;
  case Node::kDiv:
    if (lhs_in_dest) {
      ctx.asmgen.Div64(dest, reg);
    } else {
      ctx.asmgen.Div64(reg, dest);
      ctx.asmgen.Mov64(dest, reg);
    }
    break;
  case Node::kEqu:
    ctx.asmgen.CmpSet(Asm::kCmpE, dest, dest, reg);
    break;
  case Node::kNEqu:
    ctx.asmgen.CmpSet(Asm::kCmpNE, dest, dest, reg);
    break;
  case Node::kGT:
    if (auto t = MergeTypeBinOp(node->lhs->type, node->rhs->type);
        t->kind == Type::kInt) {
      ctx.asmgen.CmpSet(Asm::kCmpG, dest, lhs_reg, rhs_reg);
    } else {
      ctx.asmgen.CmpSet(Asm::kCmpA, dest, lhs_reg, rhs_reg);
    }
    break;
  case Node::kLE:
    if (auto t = MergeTypeBinOp(node->lhs->type, node->rhs->type);
        t->kind == Type::kInt) {
      ctx.asmgen.CmpSet(Asm::kCmpLE, dest, lhs_reg, rhs_reg);
    } else {
      ctx.asmgen.CmpSet(Asm::kCmpBE, dest, lhs_reg, rhs_reg);
    }
    break;
  case Node::kDefVar:
  case Node::kAssign:
    GenerateAssign(ctx, {node, dest, reg, lhs_reg, rhs_reg, lhs_in_dest},
                   free_calc_regs, lval);
    break;
  case Node::kAddr:
    break;
  case Node::kDeref:
    if (!lval) {
      ctx.asmgen.LoadN(dest, dest, 0, DataTypeOf(ctx, lhs_t));
    }
    break;
  case Node::kSubscr:
    ctx.asmgen.Mul64(rhs_reg, rhs_reg, SizeofType(ctx.src, lhs_t->base));
    ctx.asmgen.Add64(dest, reg);
    if (!lval) {
      ctx.asmgen.LoadN(dest, dest, 0, DataTypeOf(ctx, lhs_t->base));
    }
    break;
  default:
    cerr << "GenerateAsm: should not come here" << endl;
    ErrorAt(ctx.src, *node->token);
  }

  if (auto t = GetUserBaseType(node->type); !lval && IsIntegral(t)) {
    if (auto bits = get<long>(t->value); bits < 64) {
      ctx.asmgen.And64(dest, (1 << get<long>(t->value)) - 1);
    }
  }
  /* node->type が bool の場合はあえて無視する。
   * なぜなら、bool になるのは各種比較演算子のときのみで、
   * 各種比較演算子は必ず 0/1 の値を返すから。
   * int -> bool のキャスト（0 なら 0、非 0 なら 1）をせずとも、
   * 希望する結果は既に得られている。
   */
}

void PrintDebugInfo(Node* ast, vector<opela_type::String>& strings) {
  PrintASTRec(cout, ast);
  cout << '\n';
  for (size_t i = 0; i < strings.size(); ++i) {
    cout << StringLabel(i) << ": \"";
    for (auto ch : strings[i]) {
      cout << static_cast<char>(ch);
    }
    cout << "\"\n";
  }
}

int main(int argc, char** argv) {
  if (int err = ParseArgs(argc, argv)) {
    return err;
  }

  Asm* asmgen;
  if (target_arch == "x86_64") {
    asmgen = NewAsm(AsmArch::kX86_64, cout);
  } else {
    cerr << "current version doesn't support " << target_arch << endl;
    return 1;
  }

  Source src;
  src.ReadAll(cin);
  Tokenizer tokenizer(src);
  TypeManager type_manager(src);
  Scope<Object> scope;
  std::vector<opela_type::String> strings;
  list<Type*> unresolved_types;
  list<Node*> undeclared_ids;
  ASTContext ast_ctx{src, tokenizer, type_manager, scope, strings,
                     unresolved_types, undeclared_ids, nullptr};
  auto ast = Program(ast_ctx);

  if (verbosity >= 1) {
    cout << "/* AST before resolving types\n";
    PrintDebugInfo(ast, strings);
    cout << "*/\n\n";
  }
  ResolveIDs(ast_ctx);
  ResolveType(ast_ctx);
  SetTypeProgram(ast_ctx, ast);
  cout << "/* AST\n";
  PrintDebugInfo(ast, strings);
  cout << "*/\n\n";

  Asm::RegSet free_calc_regs;
  if (!asmgen->SameReg(Asm::kRegA, Asm::kRegV0)) {
    free_calc_regs.set(Asm::kRegV0);
  }
  free_calc_regs.set(Asm::kRegV1);
  free_calc_regs.set(Asm::kRegV2);
  free_calc_regs.set(Asm::kRegV3);
  free_calc_regs.set(Asm::kRegV4);
  free_calc_regs.set(Asm::kRegV5);
  free_calc_regs.set(Asm::kRegX);
  free_calc_regs.set(Asm::kRegY);

  auto globals = scope.GetGlobals();
  cout << ".intel_syntax noprefix\n";
  for (auto obj : globals) {
    if (obj->linkage == Object::kGlobal && obj->kind == Object::kFunc) {
      GenContext ctx{src, *asmgen, obj};
      GenerateAsm(ctx, obj->def, Asm::kRegA, free_calc_regs, {});
    }
  }

  asmgen->Output() << ".global _init_opela\n_init_opela:\n";
  asmgen->Push64(Asm::kRegBP);
  asmgen->Mov64(Asm::kRegBP, Asm::kRegSP);
  for (auto obj : globals) {
    if (obj->linkage == Object::kGlobal && obj->kind == Object::kVar) {
      auto var_def = obj->def;
      if (var_def->rhs && IsLiteral(var_def->rhs) == false) {
        GenContext ctx{src, *asmgen, obj};
        GenerateAsm(ctx, var_def->rhs, Asm::kRegA, free_calc_regs, {});
        auto lhs_reg = UseAnyCalcReg(free_calc_regs);
        GenerateAsm(ctx, var_def->lhs, lhs_reg, free_calc_regs, {}, true);
        GenerateAssign(
            ctx,
            {var_def, lhs_reg, Asm::kRegA, lhs_reg, Asm::kRegA, true},
            free_calc_regs, false);
      }
    }
  }
  asmgen->Output() << "_init_opela.exit:\n";
  asmgen->Leave();
  asmgen->Ret();

  asmgen->Output() << ".section .init_array\n";
  asmgen->Output() << "    .dc.a _init_opela\n";

  asmgen->Output() << ".section .data\n";
  for (size_t i = 0; i < strings.size(); ++i) {
    cout << StringLabel(i) << ":\n    .byte ";
    for (auto ch : strings[i]) {
      cout << static_cast<int>(ch) << ',';
    }
    cout << "0\n";
  }

  GenContext ctx{src, *asmgen, nullptr};
  for (auto obj : globals) {
    if (obj->linkage == Object::kGlobal && obj->kind == Object::kVar) {
      asmgen->Output() << obj->id->raw << ":\n";
      GenerateGVarData(ctx, obj->type, obj->def->rhs);
    }
  }
}
