#pragma once

#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <seastar/net/api.hh>
#include <system_error>

struct ip_result {
  char ip[INET6_ADDRSTRLEN];
  std::errc ec;
};

namespace ip_helpers {
ip_result get_ip_address(seastar::socket_address &remote);
}