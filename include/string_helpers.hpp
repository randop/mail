#pragma once

#include <cstring>
#include <string>
#include <string_view>

namespace string_helpers {
bool compare_strings_ab(const char *a, const std::string &b);

bool compare_string_views(const std::string_view &a, const std::string_view &b);
} // namespace string_helpers