#include "string_helpers.hpp"

namespace string_helpers {

/// @warning Passing `nullptr` for @p a results in undefined behavior.
bool compare_strings_ab(const char *a, const std::string &b) {
  if (!a) {
    return false;
  }

  size_t i = 0;

  auto to_lower = [](unsigned char c) constexpr {
    return static_cast<char>(std::tolower(c));
  };

  size_t b_size = b.size();

  while (a[i] != '\0' && i < b_size) {
    if (to_lower(static_cast<unsigned char>(a[i])) !=
        to_lower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
    if ((i + 1) > b_size) {
      break;
    }
    ++i;
  }

  return i == b_size;
}

bool compare_string_views(const std::string_view &a,
                          const std::string_view &b) {
  if (a.size() == 0 || b.size() == 0) {
    return false;
  }

  size_t i = 0;

  auto to_lower = [](unsigned char c) constexpr {
    return static_cast<char>(std::tolower(c));
  };

  size_t b_size = b.size();

  while (a[i] != '\0' && i < b_size) {
    if (to_lower(static_cast<unsigned char>(a[i])) !=
        to_lower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
    if ((i + 1) > b_size) {
      break;
    }
    ++i;
  }

  return i == b_size;
}
} // namespace string_helpers