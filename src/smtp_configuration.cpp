#include "smtp_configuration.hpp"

smtp_configuration::smtp_configuration() {
  add(&_host, "host", true);
  add(&_port, "port", true);
  add(&_budget, "budget");
  add(&_data_directory, "data_directory");
  add(&_log_directory, "log_directory");
  add(&_certificate, "certificate");
  add(&_privatekey, "privatekey");
  add(&_domain, "domain");
  add(&_all_email_domains, "all_email_domains");
  add(&_proxy_support, "proxy_support");
  add(&_email_limit_size, "email_limit_size");
  add(&_session_timeout, "session_timeout");
}

const std::string &smtp_configuration::host() const { return _host(); }
uint16_t smtp_configuration::port() const { return _port(); }
resource_budget_t smtp_configuration::budget() const {
  return static_cast<resource_budget_t>(_budget());
}
const std::string &smtp_configuration::data_directory() const {
  return _data_directory();
}
const std::string &smtp_configuration::log_directory() const {
  return _log_directory();
}

const std::string &smtp_configuration::certificate() const {
  return _certificate();
}
const std::string &smtp_configuration::privatekey() const {
  return _privatekey();
}

const std::string &smtp_configuration::domain() const { return _domain(); }
const std::string &smtp_configuration::all_email_domains() const {
  return _all_email_domains();
}

bool smtp_configuration::proxy_support() const { return _proxy_support(); }

size_t smtp_configuration::email_limit_size() const {
  return _email_limit_size();
}

uint32_t smtp_configuration::session_timeout() const {
  return _session_timeout();
}

void smtp_configuration::set_host(std::string v) { _host = std::move(v); }
void smtp_configuration::set_port(uint16_t v) {
  _port = static_cast<uint64_t>(v);
}
void smtp_configuration::set_budget(resource_budget_t b) {
  _budget = static_cast<uint16_t>(b);
}
void smtp_configuration::set_data_directory(std::string v) {
  _data_directory = std::move(v);
}
void smtp_configuration::set_log_directory(std::string v) {
  _log_directory = std::move(v);
}

void smtp_configuration::set_certificate(std::string v) {
  _certificate = std::move(v);
}
void smtp_configuration::set_privatekey(std::string v) {
  _privatekey = std::move(v);
}

void smtp_configuration::set_domain(std::string v) { _domain = std::move(v); }
void smtp_configuration::set_all_email_domains(std::string v) {
  _all_email_domains = std::move(v);
}

void smtp_configuration::set_proxy_support(bool v) { _proxy_support = v; }

void smtp_configuration::set_email_limit_size(size_t v) {
  _email_limit_size = static_cast<uint64_t>(v);
}

void smtp_configuration::set_session_timeout(uint32_t v) {
  _session_timeout = static_cast<uint64_t>(v);
}

seastar::sstring smtp_configuration::to_json_string() const {
  return json_base::to_json();
}

seastar::future<>
smtp_configuration::from_json_string(const seastar::sstring &json) {

  using boost::json::object;
  using boost::json::parse;
  using boost::json::value;

  std::string_view sv(json.data(), json.size());
  object obj = boost::json::parse(sv).as_object();

  if (obj.contains("host")) {
    _host = obj["host"].as_string().c_str();
  }

  if (obj.contains("port") && obj["port"].is_int64()) {
    _port = static_cast<uint64_t>(obj["port"].to_number<int64_t>());
  } else {
    _port = static_cast<uint64_t>(DEFAULT_SMTP_PORT);
  }

  _budget = 0;
  if (obj.contains("budget") && obj["budget"].is_int64()) {
    auto budget = static_cast<uint16_t>(obj["budget"].to_number<int64_t>());
    if (budget == 1) {
      _budget = 1;
    }
  }

  if (obj.contains("data_directory")) {
    _data_directory = obj["data_directory"].as_string().c_str();
  }

  if (obj.contains("log_directory")) {
    _log_directory = obj["log_directory"].as_string().c_str();
  }

  if (obj.contains("certificate")) {
    _certificate = obj["certificate"].as_string().c_str();
  }

  if (obj.contains("privatekey")) {
    _privatekey = obj["privatekey"].as_string().c_str();
  }

  if (obj.contains("domain")) {
    _domain = obj["domain"].as_string().c_str();
  }

  if (obj.contains("all_email_domains")) {
    _all_email_domains = obj["all_email_domains"].as_string().c_str();
  }

  if (obj.contains("proxy_support")) {
    _proxy_support = obj["proxy_support"].as_bool();
  }

  if (obj.contains("email_limit_size") && obj["email_limit_size"].is_int64()) {
    _email_limit_size =
        static_cast<uint64_t>(obj["email_limit_size"].to_number<int64_t>());
  } else {
    _email_limit_size = DEFAULT_EMAIL_SIZE_LIMIT;
  }

  if (obj.contains("session_timeout") && obj["session_timeout"].is_int64()) {
    _session_timeout =
        static_cast<uint64_t>(obj["session_timeout"].to_number<int64_t>());
  } else {
    _session_timeout = DEFAULT_TIMEOUT_SECONDS;
  }

  return seastar::make_ready_future<>();

  return seastar::make_ready_future<>();
}