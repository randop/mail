#include "email_helpers.hpp"

namespace email_helpers {
bool is_atext(char c) noexcept {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' ||
         c == '%' || c == '+' || c == '-';
}

bool is_domain_char(char c) noexcept {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-';
}

bool validate_email(std::string_view v) noexcept {
  auto at = v.find('@');
  if (at == std::string_view::npos || at == 0 || at == v.size() - 1) {
    return false;
  }

  auto local = v.substr(0, at);
  auto domain = v.substr(at + 1);

  // local-part
  for (char c : local) {
    if (!is_atext(c)) {
      return false;
    }
  }

  // domain must contain at least one dot
  if (domain.find('.') == std::string_view::npos) {
    return false;
  }

  for (char c : domain) {
    if (!is_domain_char(c)) {
      return false;
    }
  }

  return true;
}

seastar::sstring join_email_domains(const email_domains_t &domains) noexcept {
  if (domains.count == 0) {
    return {};
  }

  constexpr std::string_view separator = ", ";
  constexpr std::size_t sep_len = 2;
  const std::size_t n = domains.count;

  std::size_t total_len = sep_len * (n - 1);
  for (std::size_t i = 0; i < n; ++i) {
    total_len += domains.domains[i].size();
  }

  seastar::sstring result;
  result.resize(total_len);

  char *ptr = result.data();

  for (std::size_t i = 0; i < n; ++i) {
    const auto &domain = domains.domains[i];
    std::memcpy(ptr, domain.data(), domain.size());
    ptr += domain.size();
    if (i < n - 1) {
      std::memcpy(ptr, separator.data(), sep_len);
      ptr += sep_len;
    }
  }

  return result;
}

email_domains_t
load_email_domains(const seastar::sstring &default_email_domain) {
  email_domains_t result;
  const char *env = std::getenv("MAIL_EMAIL_DOMAINS");

  if (!env || env[0] == '\0') {
    result.domains[0] = default_email_domain;
    result.count = 1;
    return result;
  }

  seastar::sstring raw(env);
  std::size_t start = 0;
  std::size_t pos;

  while ((pos = raw.find(',', start)) != seastar::sstring::npos &&
         result.count < MAX_EMAIL_DOMAINS) {
    auto token = raw.substr(start, pos - start);
    if (!token.empty()) {
      result.domains[result.count++] = std::move(token);
    }
    start = pos + 1;
  }

  if (result.count < MAX_EMAIL_DOMAINS) {
    auto tail = raw.substr(start);
    if (!tail.empty()) {
      result.domains[result.count++] = std::move(tail);
    }
  }

  if (result.count == 0) {
    result.domains[0] = default_email_domain;
    result.count = 1;
  }

  return result;
}

email_extract_result extract_email_address(std::string_view sv) {
  if (auto l = sv.find('<'); l != std::string_view::npos) {
    if (auto r = sv.find('>', l + 1); r != std::string_view::npos) {
      auto candidate = sv.substr(l + 1, r - (l + 1));
      if (email_helpers::validate_email(candidate)) {
        return {seastar::sstring(candidate), {}};
      }
      return {"", std::errc::invalid_argument};
    }
  }

  for (size_t i = 0; i < sv.size(); ++i) {
    if (sv[i] == '@') {
      size_t start = i;
      while (start > 0 && email_helpers::is_atext(sv[start - 1])) {
        --start;
      }

      size_t end = i + 1;
      while (end < sv.size() && email_helpers::is_domain_char(sv[end])) {
        ++end;
      }

      auto candidate = sv.substr(start, end - start);
      if (email_helpers::validate_email(candidate)) {
        return {seastar::sstring(candidate), {}};
      }
    }
  }

  return {"", std::errc::result_out_of_range};
}

}; // namespace email_helpers