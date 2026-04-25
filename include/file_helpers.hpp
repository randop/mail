#pragma once

#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <seastar/core/sstring.hh>
#include <string>
#include <string_view>
#include <system_error>

#include "uuid_helpers.hpp"

namespace file_helpers {

seastar::sstring generate_random_logname();
seastar::sstring generate_email_filename();

} // namespace file_helpers