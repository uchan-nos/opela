#pragma once

#include <cstdint>
#include <memory>

struct Node {
  enum Kind {
    kAdd,
    kSub,
    kMul,
    kDiv,
    kInt,
  } kind;

  std::shared_ptr<Node> lhs, rhs;
  std::int64_t value;
};

std::shared_ptr<Node> MakeNode(Node::Kind kind,
                               const std::shared_ptr<Node>& lhs,
                               const std::shared_ptr<Node>& rhs);
std::shared_ptr<Node> MakeNodeInt(std::int64_t value);

std::shared_ptr<Node> Expr();
std::shared_ptr<Node> Mul();
std::shared_ptr<Node> Primary();
