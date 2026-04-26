#pragma once

#include <arpa/inet.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <optional>
#include <seastar/net/api.hh>
#include <system_error>

struct ip_result {
  char ip[INET6_ADDRSTRLEN];
  std::errc ec;
};

struct proxy_info {
  uint32_t src_ip;
  uint16_t src_port;
};

namespace ip_helpers {

constexpr uint8_t PROXY_V2_SIG[12] = {0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D,
                                      0x0A, 0x51, 0x55, 0x49, 0x54, 0x0A};

ip_result get_ip_address(seastar::socket_address &remote);

std::optional<proxy_info> parse_proxy_v2(const uint8_t *buf,
                                         size_t len) noexcept;
} // namespace ip_helpers