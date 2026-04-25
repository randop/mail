#pragma once

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <format>
#include <iomanip>
#include <memory>
#include <optional>
#include <seastar/core/file.hh>
#include <seastar/core/fstream.hh>
#include <seastar/core/future-util.hh>
#include <seastar/core/future.hh>
#include <seastar/core/sstring.hh>
#include <seastar/util/closeable.hh>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

static constexpr std::size_t MAX_EMAIL_DOMAINS = 12;

struct email_domains_t {
  std::array<seastar::sstring, MAX_EMAIL_DOMAINS> domains;
  std::size_t count = 0;
};

struct email_extract_result {
  seastar::sstring email;
  std::errc ec{};
};

namespace email_helpers {

email_extract_result extract_email_address(std::string_view sv);

bool is_atext(char c) noexcept;

bool is_domain_char(char c) noexcept;

bool validate_email(std::string_view v) noexcept;

seastar::sstring join_email_domains(const email_domains_t &domains) noexcept;

email_domains_t
load_email_domains(const seastar::sstring &default_email_domain);
} // namespace email_helpers