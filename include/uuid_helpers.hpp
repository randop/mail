#pragma once

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <seastar/core/lowres_clock.hh>
#include <seastar/core/sstring.hh>
#include <string>
#include <string_view>

namespace uuid_helpers {

seastar::sstring generate_v7();

constexpr uint64_t fnv1a_64(std::string_view s) noexcept;

std::string to_base62(uint64_t value);

std::string session_uuid(std::string_view uuid_v7);

} // namespace uuid_helpers