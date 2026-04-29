#pragma once

#include <arpa/inet.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <optional>
#include <seastar/net/api.hh>
#include <system_error>
#include <tuple>

struct ip_result {
  char ip[INET6_ADDRSTRLEN];
  std::errc ec;
};

struct proxy_info_t {
  uint8_t family; // 0x11 (IPv4), 0x21 (IPv6)

  union {
    struct {
      uint32_t src_ip;
      uint32_t dst_ip;
      uint16_t src_port;
      uint16_t dst_port;
    } v4;

    struct {
      uint8_t src_ip[16];
      uint8_t dst_ip[16];
      uint16_t src_port;
      uint16_t dst_port;
    } v6;
  };
};

namespace ip_helpers {

constexpr uint8_t PROXY_V2_SIG[12] = {0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D,
                                      0x0A, 0x51, 0x55, 0x49, 0x54, 0x0A};

ip_result get_ip_address(seastar::socket_address &remote);

std::optional<proxy_info_t> parse_proxy_v2(const char *header, const char *body,
                                           size_t len) noexcept;

void format_ip(const proxy_info_t &info, ip_result &out) noexcept;
} // namespace ip_helpers