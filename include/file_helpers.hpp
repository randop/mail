#pragma once

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <seastar/core/sstring.hh>
#include <string>
#include <string_view>
#include <system_error>

#include "uuid_helpers.hpp"

namespace file_helpers {

seastar::sstring generate_random_logname();

seastar::sstring generate_email_filename();

std::errc check_data_directory(const std::string &path);

bool move_file_safe(const std::filesystem::path &from,
                    const std::filesystem::path &to) noexcept;

} // namespace file_helpers