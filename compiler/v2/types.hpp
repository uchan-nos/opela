#pragma once

#include <cstdint>
#include <string>

// OpeLa 言語での型と C++ での型の対応

namespace opela_type {
  using Int = std::int64_t;
  using UInt = std::uint64_t;
  using Byte = std::uint8_t;
  using String = std::basic_string<Byte>;
}
