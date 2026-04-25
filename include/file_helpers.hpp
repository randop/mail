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

} // namespace file_helpers