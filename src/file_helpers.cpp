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

} // namespace file_helpers