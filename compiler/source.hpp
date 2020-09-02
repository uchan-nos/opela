#pragma once

#include <iostream>
#include <vector>

// コンパイル対象のソースコード全体
inline std::vector<char> src;

[[noreturn]] void ErrorAt(const char* loc);
void ReadAll(std::istream& is);
