#include "ip_helpers.hpp"

#include <iostream>

namespace ip_helpers {
ip_result get_ip_address(seastar::socket_address &remote) {
  const char *res = nullptr;
  ip_result result;

  std::strncpy(result.ip, "172.17.0.1", sizeof(result.ip) - 1);
  result.ip[sizeof(result.ip) - 1] = '\0';
  result.ec = std::errc::bad_address;

  const sockaddr &sa = remote.as_posix_sockaddr();

  if (sa.sa_family == AF_INET) {
    res =
        inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in &>(sa).sin_addr,
                  result.ip, sizeof(result.ip));
  } else {
    res = inet_ntop(AF_INET6,
                    &reinterpret_cast<const sockaddr_in6 &>(sa).sin6_addr,
                    result.ip, sizeof(result.ip));
  }

  if (res) {
    result.ec = std::errc{};
  }

  return result;
}

std::optional<proxy_info_t> parse_proxy_v2(const char *header, const char *body,
                                           size_t len) noexcept {

  const uint8_t *h = reinterpret_cast<const uint8_t *>(header);
  const uint8_t *b = reinterpret_cast<const uint8_t *>(body);

  // Validate PROXY v2 header
  if (std::memcmp(h, PROXY_V2_SIG, 12) != 0) [[unlikely]] {
    return std::nullopt;
  }

  const uint8_t ver_cmd = h[12];
  const uint8_t fam = h[13];
  const uint16_t addr_len = (uint16_t(h[14]) << 8) | uint16_t(h[15]);

  // PROXY v2 command check
  if (((ver_cmd >> 4) != 0x2) || ((ver_cmd & 0x0F) != 0x1)) [[unlikely]] {
    return std::nullopt;
  }

  if (len < addr_len) [[unlikely]] {
    return std::nullopt;
  }

  proxy_info_t out{};
  out.family = fam;

  // IPv4
  if (fam == 0x11) [[likely]] {
    if (addr_len < 12) [[unlikely]] {
      return std::nullopt;
    }

    std::memcpy(&out.v4.src_ip, b + 0, 4);
    std::memcpy(&out.v4.dst_ip, b + 4, 4);
    std::memcpy(&out.v4.src_port, b + 8, 2);
    std::memcpy(&out.v4.dst_port, b + 10, 2);

    out.v4.src_port = ntohs(out.v4.src_port);
    out.v4.dst_port = ntohs(out.v4.dst_port);

    return out;
  }

  // IPv6
  if (fam == 0x21) [[likely]] {
    if (addr_len < 36) [[unlikely]] {
      return std::nullopt;
    }

    std::memcpy(out.v6.src_ip, b + 0, 16);
    std::memcpy(out.v6.dst_ip, b + 16, 16);
    std::memcpy(&out.v6.src_port, b + 32, 2);
    std::memcpy(&out.v6.dst_port, b + 34, 2);

    out.v6.src_port = ntohs(out.v6.src_port);
    out.v6.dst_port = ntohs(out.v6.dst_port);

    return out;
  }

  return std::nullopt;
}

void format_ip(const proxy_info_t &info, ip_result &out) noexcept {
  char *p = out.ip;

  auto write_octet = [&](uint8_t v) noexcept {
    if (v >= 100) {
      *p++ = '0' + (v / 100);
      v %= 100;
      *p++ = '0' + (v / 10);
      *p++ = '0' + (v % 10);
    } else if (v >= 10) {
      *p++ = '0' + (v / 10);
      *p++ = '0' + (v % 10);
    } else {
      *p++ = '0' + v;
    }
  };

  auto write_hex16 = [&](uint16_t v) noexcept {
    static constexpr char hex[] = "0123456789abcdef";
    bool started = false;

    for (int i = 3; i >= 0; --i) {
      uint8_t nibble = (v >> (i * 4)) & 0xF;
      if (nibble || started || i == 0) {
        *p++ = hex[nibble];
        started = true;
      }
    }
  };

  // IPv4
  if (info.family == 0x11) [[likely]] {
    uint32_t ip = ntohl(info.v4.src_ip);

    write_octet((ip >> 24) & 0xFF);
    *p++ = '.';
    write_octet((ip >> 16) & 0xFF);
    *p++ = '.';
    write_octet((ip >> 8) & 0xFF);
    *p++ = '.';
    write_octet(ip & 0xFF);

    *p = '\0';
    return;
  }

  // IPv6
  if (info.family == 0x21) [[likely]] {
    uint16_t words[8];

    for (int i = 0; i < 8; ++i) {
      std::memcpy(&words[i], info.v6.src_ip + i * 2, 2);
      words[i] = ntohs(words[i]);
    }

    int best_start = -1, best_len = 0;
    for (int i = 0; i < 8;) {
      if (words[i] == 0) {
        int j = i;
        while (j < 8 && words[j] == 0) {
          ++j;
        }
        int len = j - i;
        if (len > best_len) {
          best_start = i;
          best_len = len;
        }
        i = j;
      } else {
        ++i;
      }
    }

    if (best_len < 2) {
      best_start = -1;
    }

    for (int i = 0; i < 8; ++i) {
      if (i == best_start) {
        *p++ = ':';
        *p++ = ':';
        i += best_len - 1;
        continue;
      }

      if (i > 0 && i != best_start + best_len) {
        *p++ = ':';
      }

      write_hex16(words[i]);
    }

    *p = '\0';
    return;
  }
  std::memcpy(out.ip, "0.0.0.0", 8);
}

} // namespace ip_helpers