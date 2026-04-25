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
} // namespace ip_helpers