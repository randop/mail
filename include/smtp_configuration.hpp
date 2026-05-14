#pragma once

#include <boost/json.hpp>
#include <cstdint>
#include <seastar/core/future.hh>
#include <seastar/core/shared_ptr.hh>
#include <seastar/core/sstring.hh>
#include <seastar/json/formatter.hh>
#include <seastar/json/json_elements.hh>
#include <string>

#include "constants.hpp"
#include "email_helpers.hpp"

class smtp_configuration final : public seastar::json::json_base {
public:
  smtp_configuration();

  const std::string &host() const;
  uint16_t port() const;
  resource_budget_t budget() const;
  const std::string &data_directory() const;
  const std::string &log_directory() const;
  const std::string &certificate() const;
  const std::string &privatekey() const;
  const std::string &domain() const;
  const std::string &all_email_domains() const;
  bool proxy_support() const;
  size_t email_limit_size() const;
  uint32_t session_timeout() const;

  void set_host(std::string v);
  void set_port(uint16_t v);
  void set_budget(resource_budget_t v);
  void set_data_directory(std::string v);
  void set_log_directory(std::string v);
  void set_certificate(std::string v);
  void set_privatekey(std::string v);
  void set_domain(std::string v);
  void set_all_email_domains(std::string v);
  void set_proxy_support(bool v);
  void set_email_limit_size(size_t v);
  void set_session_timeout(uint32_t v);

  seastar::sstring to_json_string() const;
  seastar::future<> from_json_string(const seastar::sstring &json);

private:
  seastar::json::json_element<std::string> _host;
  seastar::json::json_element<uint64_t> _port;
  seastar::json::json_element<uint16_t> _budget;
  seastar::json::json_element<std::string> _data_directory;
  seastar::json::json_element<std::string> _log_directory;
  seastar::json::json_element<std::string> _certificate;
  seastar::json::json_element<std::string> _privatekey;
  seastar::json::json_element<std::string> _domain;
  seastar::json::json_element<std::string> _all_email_domains;
  seastar::json::json_element<bool> _proxy_support;
  seastar::json::json_element<uint64_t> _email_limit_size;
  seastar::json::json_element<uint64_t> _session_timeout;
};
