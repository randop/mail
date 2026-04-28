#pragma once

#include <algorithm>
#include <array>
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
#include <vector>

#include "constants.hpp"

static constexpr std::size_t MAX_EMAIL_DOMAINS = 12;

static constexpr int CMD_POSITION_ZERO = 0;

struct email_domains_t {
  std::array<seastar::sstring, MAX_EMAIL_DOMAINS> domains;
  std::size_t count = 0;
};

struct email_extract_result {
  seastar::sstring email;
  std::errc ec{};
};

struct smtp_parse_result_t {
  std::string_view args;
  std::errc ec = std::errc{};
};

namespace email_helpers {

email_extract_result extract_email_address(std::string_view sv);

bool is_atext(char c) noexcept;

bool is_domain_char(char c) noexcept;

bool validate_email(std::string_view v) noexcept;

seastar::sstring join_email_domains(const email_domains_t &domains) noexcept;

email_domains_t
load_email_domains(const seastar::sstring &default_email_domain);

std::string_view trim(std::string_view v);

std::vector<seastar::sstring> split_and_trim(const seastar::sstring &input);

email_domains_t split_email_domains(const seastar::sstring &all_email_domain,
                                    const seastar::sstring &email_domain);

std::string_view get_domain(std::string_view email);

seastar::sstring extract_root_domain(const seastar::sstring &host) noexcept;

SMTP_COMMAND get_smtp_command(std::string_view cmd) noexcept;

std::string_view smtp_command_string(const SMTP_COMMAND &cmd);

smtp_parse_result_t
parse_smtp_line(const SMTP_COMMAND &cmd,
                const std::string_view &buffer_view) noexcept;

} // namespace email_helpers