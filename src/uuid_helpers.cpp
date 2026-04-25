#include "uuid_helpers.hpp"

namespace uuid_helpers {

seastar::sstring generate_v7() {
  auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    seastar::lowres_system_clock::now().time_since_epoch())
                    .count();
  thread_local std::mt19937_64 rng{std::random_device{}()};
  std::uniform_int_distribution<uint64_t> dist;

  uint8_t b[16];
  uint64_t ts = static_cast<uint64_t>(now_ms);
  b[0] = static_cast<uint8_t>((ts >> 40) & 0xff);
  b[1] = static_cast<uint8_t>((ts >> 32) & 0xff);
  b[2] = static_cast<uint8_t>((ts >> 24) & 0xff);
  b[3] = static_cast<uint8_t>((ts >> 16) & 0xff);
  b[4] = static_cast<uint8_t>((ts >> 8) & 0xff);
  b[5] = static_cast<uint8_t>(ts & 0xff);

  uint64_t rand_a = dist(rng);
  uint64_t rand_b = dist(rng);
  b[6] = static_cast<uint8_t>((rand_a >> 56) & 0x0f) | 0x70;
  b[7] = static_cast<uint8_t>((rand_a >> 48) & 0xff);
  b[8] = static_cast<uint8_t>((rand_a >> 40) & 0x3f) | 0x80;
  b[9] = static_cast<uint8_t>((rand_a >> 32) & 0xff);
  b[10] = static_cast<uint8_t>((rand_a >> 24) & 0xff);
  b[11] = static_cast<uint8_t>((rand_a >> 16) & 0xff);
  b[12] = static_cast<uint8_t>((rand_a >> 8) & 0xff);
  b[13] = static_cast<uint8_t>(rand_a & 0xff);
  b[14] = static_cast<uint8_t>((rand_b >> 56) & 0xff);
  b[15] = static_cast<uint8_t>((rand_b >> 48) & 0xff);

  static const char hex[] = "0123456789abcdef";
  seastar::sstring out{seastar::sstring::initialized_later(), 36};
  auto *p = out.data();
  for (int i = 0; i < 4; ++i) {
    *p++ = hex[b[i] >> 4], *p++ = hex[b[i] & 0xf];
  }
  *p++ = '-';
  for (int i = 4; i < 6; ++i) {
    *p++ = hex[b[i] >> 4], *p++ = hex[b[i] & 0xf];
  }
  *p++ = '-';
  for (int i = 6; i < 8; ++i) {
    *p++ = hex[b[i] >> 4], *p++ = hex[b[i] & 0xf];
  }
  *p++ = '-';
  for (int i = 8; i < 10; ++i) {
    *p++ = hex[b[i] >> 4], *p++ = hex[b[i] & 0xf];
  }
  *p++ = '-';
  for (int i = 10; i < 16; ++i) {
    *p++ = hex[b[i] >> 4], *p++ = hex[b[i] & 0xf];
  }
  return out;
}
} // namespace uuid_helpers