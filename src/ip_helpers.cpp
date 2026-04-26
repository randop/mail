#include "ip_helpers.hpp"

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

std::optional<proxy_info> parse_proxy_v2(const uint8_t *buf,
                                         size_t len) noexcept {
  if (len < 16) {
    return std::nullopt;
  }

  if (std::memcmp(buf, PROXY_V2_SIG, 12) != 0) {
    return std::nullopt;
  }

  uint8_t ver_cmd = buf[12];
  uint8_t fam = buf[13];
  uint16_t addr_len = (buf[14] << 8) | buf[15];

  // Only accept PROXY command
  if ((ver_cmd >> 4) != 0x2 || (ver_cmd & 0x0F) != 0x1) {
    return std::nullopt;
  }

  // IPv4 TCP
  if (fam == 0x11) {
    if (addr_len < 12 || len < 16 + 12) {
      return std::nullopt;
    }

    const uint8_t *addr = buf + 16;

    proxy_info info{};
    std::memcpy(&info.src_ip, addr, 4);
    std::memcpy(&info.src_port, addr + 8, 2);

    info.src_port = ntohs(info.src_port);
    return info;
  }

  return std::nullopt;
}
} // namespace ip_helpers