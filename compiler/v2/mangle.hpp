#pragma once

#include <string>
#include <string_view>

struct Type;

std::string Mangle(Type* t);
std::string Mangle(std::string_view base_name, Type* t);
