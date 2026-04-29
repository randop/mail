#pragma once

#include <cerrno>
#include <cstdio>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <string>
#include <system_error>

struct x509_parse_result {
  std::string common_name;
  std::errc ec;
};

namespace x509_helpers {
std::errc map_errno(int e) noexcept;

std::string extract_common_name(X509 *cert);

x509_parse_result parse_x509(const std::string &file_path);

std::errc check_private_key(const std::string &file_path);
} // namespace x509_helpers