#include "file_helpers.hpp"

namespace file_helpers {

seastar::sstring generate_random_logname() {
  seastar::sstring out{seastar::sstring::initialized_later(), 45};
  auto *p = out.data();
  *p++ = 's';
  *p++ = 'm';
  *p++ = 't';
  *p++ = 'p';
  *p++ = '-';
  seastar::sstring random_uuid = uuid_helpers::generate_v7();
  for (char c : random_uuid) {
    if (c != '\n') {
      *p++ = c;
    }
  }
  *p++ = '.';
  *p++ = 'l';
  *p++ = 'o';
  *p++ = 'g';
  return out;
}

seastar::sstring generate_email_filename() {
  seastar::sstring out{seastar::sstring::initialized_later(), 40};
  auto *p = out.data();
  seastar::sstring random_uuid = uuid_helpers::generate_v7();
  for (char c : random_uuid) {
    if (c != '\n') {
      *p++ = c;
    }
  }
  *p++ = '.';
  *p++ = 'e';
  *p++ = 'm';
  *p++ = 'l';
  return out;
}

std::errc checktest_directory(const std::string &directory) {
  auto path = std::filesystem::path(directory);
  std::error_code ec;
  if (!std::filesystem::exists(path, ec) || ec) {
    return std::errc::no_such_file_or_directory;
  }

  if (!std::filesystem::is_directory(path, ec) || ec) {
    return std::errc::is_a_directory;
  }

  std::string filename = ".test-" + uuid_helpers::generate_v7() + ".tmp";

  auto test_file = path / filename;

  std::ofstream f(test_file, std::ios::out | std::ios::trunc);
  if (!f.is_open()) {
    return std::errc::permission_denied;
  }

  f << "ok";
  f.close();

  std::filesystem::remove(test_file, ec);
  if (ec) {
    return std::errc::permission_denied;
  }

  return std::errc();
}

bool move_file_safe(const std::filesystem::path &from,
                    const std::filesystem::path &to) noexcept {
  std::error_code ec;
  namespace fs = std::filesystem;
  fs::rename(from, to, ec);
  if (!ec) {
    return true;
  }

  ec.clear();

  fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
  if (ec) {
    return false;
  }

  fs::remove(from, ec);
  return !ec;
}

} // namespace file_helpers