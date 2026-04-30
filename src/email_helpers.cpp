#include "email_helpers.hpp"

namespace email_helpers {

bool is_atext(char c) noexcept {
  switch (c) {
  case '!':
  case '#':
  case '$':
  case '%':
  case '&':
  case '\'':
  case '*':
  case '+':
  case '/':
  case '=':
  case '?':
  case '^':
  case '_':
  case '`':
  case '{':
  case '|':
  case '}':
  case '~':
  case '-':
    return true;
  default:
    return std::isalnum(static_cast<unsigned char>(c));
  }
}

bool is_qtext(char c) noexcept {
  return (c >= 33 && c <= 126 && c != '"' && c != '\\');
}

bool is_domain_char(char c) noexcept {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.';
}

bool validate_local(std::string_view local) noexcept {
  if (local.empty()) {
    return false;
  }

  if (local.front() == '"') {
    if (local.size() < 2 || local.back() != '"') {
      return false;
    }

    bool escape = false;
    for (size_t i = 1; i < local.size() - 1; ++i) {
      char c = local[i];

      if (escape) {
        if (static_cast<unsigned char>(c) > 127) {
          return false;
        }
        escape = false;
        continue;
      }

      if (c == '\\') {
        escape = true;
        continue;
      }

      if (!is_qtext(c)) {
        return false;
      }
    }

    return !escape;
  }

  if (local.front() == '.' || local.back() == '.') {
    return false;
  }

  bool prev_dot = false;
  for (char c : local) {
    if (c == '.') {
      if (prev_dot) {
        return false;
      }
      prev_dot = true;
      continue;
    }
    if (!is_atext(c)) {
      return false;
    }
    prev_dot = false;
  }

  return true;
}

bool validate_domain(std::string_view domain) noexcept {
  if (domain.size() < 3) {
    return false;
  }

  bool has_dot = false;
  bool label_start = true;
  char prev = '\0';

  for (char c : domain) {
    if (!is_domain_char(c)) {
      return false;
    }

    if (c == '.') {
      if (label_start || prev == '.') {
        return false;
      }
      if (prev == '-') {
        return false;
      }
      has_dot = true;
      label_start = true;
    } else {
      if (label_start && c == '-') {
        return false;
      }
      label_start = false;
    }

    prev = c;
  }

  if (prev == '-' || prev == '.') {
    return false;
  }
  if (!has_dot) {
    return false;
  }

  return true;
}

bool validate_email(std::string_view v) noexcept {
  const auto at = v.find('@');
  if (at == std::string_view::npos || at == 0 || at + 1 >= v.size()) {
    return false;
  }

  const auto local = v.substr(0, at);
  const auto domain = v.substr(at + 1);

  return validate_local(local) && validate_domain(domain);
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

    const seastar::sstring root_domain =
        extract_root_domain(default_email_domain);
    if (root_domain != default_email_domain) {
      result.domains[1] = root_domain;
      result.count = 2;
    }

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

  const seastar::sstring root_domain =
      extract_root_domain(default_email_domain);
  if (root_domain != default_email_domain &&
      ((result.count + 1) < MAX_EMAIL_DOMAINS)) {
    result.count++;
    result.domains[result.count] = root_domain;
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

std::string_view trim(std::string_view v) {
  auto is_space = [](char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
  };

  while (!v.empty() && is_space(v.front())) {
    v.remove_prefix(1);
  }

  while (!v.empty() && is_space(v.back())) {
    v.remove_suffix(1);
  }

  return v;
}

std::vector<seastar::sstring> split_and_trim(const seastar::sstring &input) {
  std::vector<seastar::sstring> out;

  std::string_view sv(input);

  while (!sv.empty()) {
    size_t pos = sv.find(',');

    std::string_view token =
        (pos == std::string_view::npos) ? sv : sv.substr(0, pos);

    token = trim(token);

    if (!token.empty()) {
      out.emplace_back(token);
    }

    if (pos == std::string_view::npos) {
      break;
    }

    sv.remove_prefix(pos + 1);
  }

  return out;
}

email_domains_t split_email_domains(const seastar::sstring &all_email_domains,
                                    const seastar::sstring &email_domain) {
  email_domains_t result;

  auto parts = split_and_trim(all_email_domains);

  if (parts.size() == 0) {
    result.domains[0] = email_domain;
    result.count = 1;
    return result;
  }

  result.count = 0;
  for (auto &x : parts) {
    result.domains[result.count] = x;
    result.count++;
  }

  return result;
}

std::string_view get_domain(std::string_view email) {
  size_t pos = email.find('@');
  if (pos == std::string_view::npos || pos + 1 >= email.size()) {
    return {};
  }
  return email.substr(pos + 1);
}

seastar::sstring extract_root_domain(const seastar::sstring &host) noexcept {
  const std::string_view sv{host.data(), host.size()};

  const std::size_t last = sv.rfind('.');
  if (last == std::string_view::npos || last == 0) [[unlikely]] {
    return host;
  }

  const std::size_t prev = sv.rfind('.', last - 1);
  if (prev == std::string_view::npos) [[unlikely]] {
    return host;
  }

  return seastar::sstring{sv.data() + prev + 1, sv.size() - prev - 1};
}

SMTP_COMMAND get_smtp_command(std::string_view cmd) noexcept {
  for (const auto &[cmd_str, cmd_enum] : SMTP_RFC_COMMANDS) {
    if (cmd.starts_with(cmd_str)) {
      return cmd_enum;
    }
  }
  return SMTP_COMMAND::UNKNOWN;
}

std::string_view smtp_command_string(const SMTP_COMMAND &cmd) {
  for (const auto &[cmd_str, cmd_enum] : SMTP_RFC_COMMANDS) {
    if (cmd_enum == cmd) {
      return cmd_str;
    }
  }
  return SMTP_UNKNOWN;
}

smtp_parse_result_t
parse_smtp_line(const SMTP_COMMAND &cmd,
                const std::string_view &buffer_view) noexcept {
  const size_t crlf_pos = buffer_view.find(SMTP_CRLF);
  if (crlf_pos == std::string_view::npos) {
    return {{}, std::errc::message_size};
  }

  std::string_view line = buffer_view.substr(0, crlf_pos);

  size_t space_pos = line.find(' ');
  if (cmd == SMTP_COMMAND::MAIL || cmd == SMTP_COMMAND::RCPT ||
      cmd == SMTP_COMMAND::SEND || cmd == SMTP_COMMAND::SOML) {
    space_pos = line.find(":");
  } else if (cmd == SMTP_COMMAND::RSET || cmd == SMTP_COMMAND::NOOP ||
             cmd == SMTP_COMMAND::QUIT || cmd == SMTP_COMMAND::VRFY ||
             cmd == SMTP_COMMAND::STARTTLS) {
    return {{}, std::errc{}};
  }

  std::string_view args;

  if (space_pos != std::string_view::npos) {
    args = line.substr(space_pos + 1);
  }

  if (line.empty() || line.find_first_not_of(" \t") == std::string_view::npos) {
    return {{}, std::errc::invalid_argument};
  }

  return {args, std::errc{}};
}

}; // namespace email_helpers