#pragma once

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <seastar/core/sstring.hh>
#include <seastar/net/api.hh>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include "uuid_helpers.hpp"

namespace file_helpers {

seastar::sstring generate_random_logname();

seastar::sstring generate_email_filename();

std::errc checktest_directory(const std::string &directory);

bool move_file_safe(const std::filesystem::path &from,
                    const std::filesystem::path &to) noexcept;

std::errc delete_file(const std::string &file_path);

} // namespace file_helpers