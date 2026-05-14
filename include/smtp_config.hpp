#pragma once

#include "constants.hpp"
#include "email_helpers.hpp"

struct smtp_config_t {
  std::string host;
  uint16_t port;
  std::string data_directory;
  std::string log_directory;
  std::string certificate;
  std::string privatekey;
  resource_budget_t budget;
  std::string domain;
  email_domains_t all_email_domains;
  bool proxy_support;
  size_t email_limit_size;
  uint32_t session_timeout;
};