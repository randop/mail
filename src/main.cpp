/*****************************************************************************
Mail (https://gitlab.com/randop/mail)
  Privacy-first and self-hosted email server
  for modern-era 2026 instead of legacy 1999

Requires: Seastar v25.05.0

Copyright (c) 2010 - 2026 Randolph Ledesma. All rights reserved.

Use of this file and software is governed by the Business Source License 1.1
included in the LICENSE file (or at https://mariadb.com/bsl11/).

*** AI / MACHINE LEARNING TRAINING PROHIBITION ***

This source code is intended for human use only. Any use of this code
(or any portion, derivative, or output generated from it) for training,
fine-tuning, or improving any artificial intelligence, machine learning,
large language model, or generative system is strictly prohibited.

This prohibition applies regardless of whether the training is commercial,
non-commercial, public, or private. Violation of this term may result in
copyright infringement and other legal claims.

Change Date:    Four years from the date the Licensed Work is published.
Change License: Apache License 2.0

On the Change Date (or the fourth anniversary of the first public distribution
of this version of the software under this license, whichever comes first),
this software will be made available under the specified Change License.

*****************************************************************************/

#include <boost/program_options.hpp>
#include <seastar/core/app-template.hh>
#include <seastar/core/circular_buffer_fixed_capacity.hh>
#include <seastar/core/coroutine.hh>
#include <seastar/core/file.hh>
#include <seastar/core/fstream.hh>
#include <seastar/core/future-util.hh>
#include <seastar/core/future.hh>
#include <seastar/core/gate.hh>
#include <seastar/core/queue.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/seastar.hh>
#include <seastar/core/semaphore.hh>
#include <seastar/core/sharded.hh>
#include <seastar/core/shared_ptr.hh>
#include <seastar/core/sleep.hh>
#include <seastar/core/smp.hh>
#include <seastar/core/sstring.hh>
#include <seastar/core/timer-set.hh>
#include <seastar/net/api.hh>
#include <seastar/net/tls.hh>
#include <seastar/util/closeable.hh>
#include <seastar/util/log.hh>
#include <seastar/util/tmp_file.hh>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <format>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "constants.hpp"
#include "dma_file_writer.hpp"
#include "email_helpers.hpp"
#include "file_helpers.hpp"
#include "ip_helpers.hpp"
#include "logger.hpp"
#include "smtp_config.hpp"
#include "smtp_session.hpp"
#include "stop_signal.hh"
#include "string_helpers.hpp"
#include "uuid_helpers.hpp"
#include "x509_helpers.hpp"

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

using namespace seastar;
using namespace string_helpers;
using namespace file_helpers;

seastar::future<> handle_connection(seastar::connected_socket cs,
                                    seastar::socket_address remote,
                                    seastar::abort_source &as,
                                    shared_ptr<tls::server_credentials> certs,
                                    const smtp_config_t &smtp_config) {

  sstring sid_uuid = uuid_helpers::generate_v7();
  std::string_view sid_view(sid_uuid.data(), sid_uuid.size());
  sstring sid = uuid_helpers::session_uuid(sid_view);

  auto ip_info = ip_helpers::get_ip_address(remote);
  auto &ip = ip_info.ip;
  std::string sep(1, std::filesystem::path::preferred_separator);
  sstring email_real_filename = generate_email_filename();
  seastar::sstring email_filename =
      smtp_config.data_directory + sep + std::string(email_real_filename);
  sstring log_filename =
      smtp_config.log_directory + sep + std::string(generate_random_logname());
  applog.info("{} new client {} connection, session logging on: {}", sid,
              remote, log_filename);

  std::unique_ptr<smtp_session> session =
      std::make_unique<smtp_session>(std::move(cs));

  seastar::timer<> idle_timer;

  bool active = true;
  bool session_state_transaction_ok = false;
  bool session_state_data_written = false;

  auto sub = as.subscribe([&]() noexcept {
    active = false;
    idle_timer.cancel();
    applog.warn("{} aborting client {} ...", sid, ip);
  });

  idle_timer.set_callback([&, ip] {
    active = false;
    applog.warn("{} client {} timeout, closing ...", sid, ip);
  });
  idle_timer.arm(std::chrono::seconds(smtp_config.session_timeout));

  constexpr size_t fixed_stream_capacity = 16;
  std::vector<char> fixed_stream(fixed_stream_capacity, '\0');
  size_t fixed_stream_size = 0;
  size_t session_state_command_index = 0;

  SMTP_COMMAND session_state_cmd{SMTP_COMMAND::UNKNOWN};

  SMTP_SESSION_STATUS session_state_status{SMTP_SESSION_STATUS::COMMAND};

  size_t session_state_rcpt_count = 0;
  size_t session_state_mailfrom_count = 0;

  bool session_state_proxy_header_read = false;

  try {
    co_await session->init_logfile(log_filename);

    if (smtp_config.proxy_support) {
      session_state_proxy_header_read = true;
      if (session_state_proxy_header_read) {
        session_state_proxy_header_read = false;
        const auto header = co_await session->read_input_exactly(16);
        const auto *h = header.get();
        uint16_t header_length = (h[14] << 8) | h[15];
        auto body = co_await session->read_input_exactly(header_length);
        auto proxy_info =
            ip_helpers::parse_proxy_v2(header.get(), body.get(), header_length);
        if (!proxy_info) [[unlikely]] {
          applog.error("{} proxy protocol violation of client {}", sid, remote);
          active = false;
        } else {
          ip_helpers::format_ip(*proxy_info, ip_info);
          applog.info("{} detected real ip {} of client {}", sid, ip, remote);

          sstring ready =
              std::format("220 {} Service ready\r\n", smtp_config.domain);
          co_await session->send(std::move(ready));
        }
      }
    } else {
      sstring ready =
          std::format("220 {} Service ready\r\n", smtp_config.domain);
      co_await session->send(std::move(ready));
    }

    while (active) {
      seastar::temporary_buffer<char> buffer_stream =
          co_await session->read_input();
      if (buffer_stream.empty()) {
        break;
      }

      auto command_stream = buffer_stream.share();

      idle_timer.rearm(seastar::timer<>::clock::now() +
                       std::chrono::seconds(smtp_config.session_timeout));

      std::string_view buffer_view = {command_stream.get(),
                                      command_stream.size()};

      if (session_state_status == SMTP_SESSION_STATUS::COMMAND) {
        session_state_command_index = 0;

        seastar::sstring line(command_stream.get(), command_stream.size());

        for (const auto &[cmd, smtp_cmd] : SMTP_RFC_COMMANDS) {
          std::string_view chunk_view = {line.data(), cmd.size()};
          if (string_helpers::compare_string_views(chunk_view, cmd)) {
            session_state_cmd = smtp_cmd;
            session_state_command_index += cmd.size();
            break;
          }
        }

        if (session_state_cmd == SMTP_COMMAND::UNKNOWN) {
          // TODO: lookup command on non-regular mode
        }
      }

      // zero-copy view of only the last segment of buffer_stream
      size_t last_take = std::min(buffer_stream.size(), fixed_stream_capacity);
      size_t start_offset = buffer_stream.size() - last_take;
      auto last_segment = buffer_stream.share(start_offset, last_take);

      // append the last segment to fixed_stream
      for (char c : last_segment) {
        if (fixed_stream_size < fixed_stream_capacity) {
          fixed_stream[fixed_stream_size] = c;
          ++fixed_stream_size;
        } else {
          // drop oldest character and append new one
          std::rotate(fixed_stream.begin(), fixed_stream.begin() + 1,
                      fixed_stream.end());
          fixed_stream.back() = c;
        }
      }

      if (session_state_status == SMTP_SESSION_STATUS::DATA) {
        auto email_stream = buffer_stream.share();
        co_await session->write_data(std::move(email_stream));
        session_state_data_written = true;

        std::string_view fixed_view(fixed_stream.data(), fixed_stream_capacity);
        size_t pos = fixed_view.find(SMTP_DATA_END);

        if (pos != std::string_view::npos) {
          session_state_status = SMTP_SESSION_STATUS::COMMAND;
          session_state_transaction_ok = true;
          sstring message = "250 OK: message queued\r\n";
          co_await session->send(std::move(message));
        } else {
          if (session->get_email_size() > DEFAULT_EMAIL_SIZE_LIMIT) {
            session_state_transaction_ok = false;

            sstring message = "552 5.3.4 Message size limit exceeded\r\n";
            co_await session->send(std::move(message));
            applog.error("{} client {} exceeded message size limit", sid, ip);
            active = false;
            break;
          }
        }
      } else {
        co_await session->write_log(buffer_stream.share());

        if (session_state_status == SMTP_SESSION_STATUS::COMMAND &&
            session_state_cmd != SMTP_COMMAND::UNKNOWN) {
          size_t pos = buffer_view.find(SMTP_CRLF);
          if (pos != std::string_view::npos) {

            auto [args, parse_ec] =
                email_helpers::parse_smtp_line(session_state_cmd, buffer_view);

            if (parse_ec == std::errc()) {
              std::string_view cmd_string =
                  email_helpers::smtp_command_string(session_state_cmd);
              applog.info("{} parsed command: {}", sid, cmd_string);

              switch (session_state_cmd) {
              case SMTP_COMMAND::HELO:
              case SMTP_COMMAND::EHLO: {
                session_state_mailfrom_count = 0;
                session_state_rcpt_count = 0;
                sstring message = std::format(
                    "250-{} Nice to meet you, "
                    "[{}]\r\n250-8BITMIME\r\n250-SMTPUTF8\r\n250-"
                    "STARTTLS\r\n250 SIZE {}\r\n",
                    smtp_config.domain, ip, smtp_config.email_limit_size);
                co_await session->send(std::move(message));
                break;
              }
              case SMTP_COMMAND::STARTTLS: {
                sstring message = "220 Ready to start TLS\r\n";
                co_await session->send(std::move(message));
                co_await session->upgrade_tls(certs);
                applog.info("{} session of {} upgraded to TLS", sid, ip);
                break;
              }
              case SMTP_COMMAND::MAIL: {
                auto [email, ec] = email_helpers::extract_email_address(args);

                if (ec == std::errc()) {
                  session_state_mailfrom_count++;
                  sstring message = "250 Accepted\r\n";
                  co_await session->send(std::move(message));
                } else {
                  sstring message = "501 5.1.3 Bad email address syntax\r\n";
                  co_await session->send(std::move(message));
                }

                break;
              }
              case SMTP_COMMAND::RCPT: {
                auto [email, ec] = email_helpers::extract_email_address(args);
                if (ec == std::errc()) {
                  sstring message = "250 Accepted\r\n";
                  co_await session->send(std::move(message));
                  std::string_view check_email =
                      email_helpers::get_domain(email);
                  if (smtp_config.all_email_domains.count == 1 &&
                      compare_string_views(check_email, smtp_config.domain)) {
                    session_state_rcpt_count++;
                  } else {
                    for (size_t i = 0; i < smtp_config.all_email_domains.count;
                         i++) {
                      if (compare_string_views(
                              check_email,
                              smtp_config.all_email_domains.domains[i])) {
                        session_state_rcpt_count++;
                        break;
                      }
                    }
                  }
                } else {
                  sstring message = "553 5.1.3 Bad email address syntax\r\n";
                  co_await session->send(std::move(message));
                }

                break;
              }
              case SMTP_COMMAND::DATA: {
                if (session_state_rcpt_count >= 1 &&
                    session_state_mailfrom_count >= 1) {
                  session_state_status = SMTP_SESSION_STATUS::DATA;
                  co_await session->init_emailfile(email_filename);
                  sstring message =
                      "354 Start mail input; end with <CR><LF>.<CR><LF>\r\n";
                  co_await session->send(std::move(message));
                } else {
                  if (session_state_rcpt_count == 0) {
                    sstring message = "554 No valid recipients\r\n";
                    co_await session->send(std::move(message));
                  } else {
                    sstring message = "503 Bad sequence of commands\r\n";
                    co_await session->send(std::move(message));
                  }
                }
                break;
              }
              case SMTP_COMMAND::RSET: {
                session_state_rcpt_count = 0;
                session_state_mailfrom_count = 0;
                sstring message = "250 OK\r\n";
                co_await session->send(std::move(message));
                break;
              }
              case SMTP_COMMAND::NOOP: {
                sstring message = "250 OK\r\n";
                co_await session->send(std::move(message));
                break;
              }
              case SMTP_COMMAND::BDAT:
              case SMTP_COMMAND::VRFY:
              case SMTP_COMMAND::SEND:
              case SMTP_COMMAND::SOML:
              case SMTP_COMMAND::AUTH: {
                sstring message = "502 Command not implemented\r\n";
                co_await session->send(std::move(message));
                break;
              }
              case SMTP_COMMAND::QUIT: {
                sstring message = "221 Bye\r\n";
                co_await session->send(std::move(message));
                active = false;
                break;
              }
              case SMTP_COMMAND::UNKNOWN:
              default:
                sstring message = "500 Syntax error\r\n";
                co_await session->send(std::move(message));
                break;
              }

              // reset
              if (session_state_status == SMTP_SESSION_STATUS::COMMAND) {
                session_state_cmd = SMTP_COMMAND::UNKNOWN;
              }
            }
          }
        } else if (session_state_status == SMTP_SESSION_STATUS::COMMAND &&
                   session_state_cmd == SMTP_COMMAND::UNKNOWN) {
          sstring message = "500 Syntax error, command unrecognized\r\n";
          co_await session->send(std::move(message));
        }

        if (session->get_log_size() > SMTP_COMMAND_BUFFER_SIZE_LIMIT) {
          applog.error("{} client {} exceeded command buffer limit", sid, ip);
          sstring message = "552 5.3.4 Message size limit exceeded\r\n";
          co_await session->send(std::move(message));
        }
      }
    }

  } catch (const seastar::timed_out_error &err) {
    applog.warn("{} client {} idle timeout", sid, ip);
  } catch (const std::exception_ptr &ep) {
    try {
      std::rethrow_exception(ep);
    } catch (const std::system_error &e) {
      applog.error("{} system error (errno {}): {} - what(): {}", sid,
                   e.code().value(), e.code().message(), e.what());
    } catch (const std::exception &e) {
      applog.error("{} exception caught: what() = {}", sid, e.what());
    } catch (...) {
      applog.error("{} unknown exception while processing", sid);
    }
  } catch (const std::exception &ex) {
    applog.warn("{} connection {} error: {}", sid, ip, ex.what());
  }

  if (session_state_transaction_ok) {
    applog.info("{} written client {} session logs on {}", sid, ip,
                log_filename);
  } else {
    applog.warn("{} email transaction failure, see logs on {} on client {}",
                sid, log_filename, ip);
  }

  if (session_state_data_written) {
    applog.info("{} new mail received on {}", sid, email_filename);
  }
  idle_timer.cancel();

  try {
    co_await session->close();
  } catch (...) {
    // void
  }

  session.reset();

  applog.info("{} client [{}] {} connection finished.", sid, ip, remote);

  co_return;
}

seastar::future<> serve(smtp_config_t &smtp_config, seastar::abort_source &as,
                        seastar::gate &gate) {

  applog.trace("Loading X.509 certificates {}, {} ...", smtp_config.certificate,
               smtp_config.privatekey);
  auto certs = make_shared<tls::server_credentials>();
  co_await certs->set_x509_key_file(smtp_config.certificate,
                                    smtp_config.privatekey,
                                    tls::x509_crt_format::PEM);
  seastar::listen_options opts;
  opts.reuse_address = true;
  opts.lba =
      seastar::server_socket::load_balancing_algorithm::connection_distribution;

  auto ss =
      seastar::listen(seastar::make_ipv4_address({smtp_config.port}), opts);

  applog.info("shard {} listening on 0.0.0.0:{}", seastar::this_shard_id(),
              smtp_config.port);
  seastar::timer<> timer;
  uint64_t connection_count = 0;
  bool stats = false;
  timer.set_callback([&connection_count, &timer, &stats] {
    if (stats) {
      applog.info("Active connections on shard {}: {}",
                  seastar::this_shard_id(), connection_count);
    }
    timer.rearm(seastar::timer<>::clock::now() + std::chrono::seconds(30));
  });
  timer.arm(std::chrono::seconds(30));

  auto sub_opt = as.subscribe([&]() noexcept {
    timer.cancel();
    ss.abort_accept();
  });

  // TODO: Detect system resource capabilities for optimal configuration
  size_t connection_limit = 1024;
  if (smtp_config.budget == resource_budget_t::ECONOMY) {
    connection_limit = 24;
  }
  seastar::semaphore connect_semaphore(connection_limit);

  while (!as.abort_requested()) {
    try {
      co_await connect_semaphore.wait();

      auto ar = co_await ss.accept();
      auto addr = ar.remote_address;

      connection_count++;

      (void)seastar::with_gate(
          gate,
          [conn = std::move(ar.connection), addr, &as, certs,
           &smtp_config]() mutable -> seastar::future<> {
            try {
              co_await handle_connection(std::move(conn), addr, as, certs,
                                         smtp_config);
            } catch (std::exception_ptr ep) {
              try {
                std::rethrow_exception(ep);
              } catch (const std::exception &ex) {
                applog.error("Error closing connection: {}", ex.what());
              }
            } catch (const std::exception &ex) {
              applog.error("Error closing connection: {}", ex.what());
            }
          })
          .finally([&connect_semaphore, &connection_count] mutable {
            connect_semaphore.signal();
            connection_count--;
            applog.trace("finally closing connections...");
          });
    } catch (const seastar::abort_requested_exception &) {
      break;
    } catch (const std::exception &ex) {
      if (!as.abort_requested()) {
        applog.error("accept failed: {}", ex.what());
      }
    }
  }
  timer.cancel();
  ss.abort_accept();
  applog.info("shard {} stopped", seastar::this_shard_id());
}

int main(int argc, char **argv) {
  char **it = std::find_if(argv, argv + argc, [](const char *arg) {
    return std::string_view(arg) == "--version";
  });

  if (it != argv + argc) {
    std::cout << PROJECT_VERSION << std::endl;
    return EXIT_SUCCESS;
  }

  bool show_help = false;

  char **help_it = std::find_if(argv, argv + argc, [](const char *arg) {
    return std::string_view(arg) == "--help";
  });

  if (help_it != argv + argc) {
    show_help = true;
  }

  applog.info("mail version {}", PROJECT_VERSION);
  const std::span<char *const> args{argv, static_cast<std::size_t>(argc)};

  std::vector<std::string> args_vector;
  args_vector.reserve(args.size());
  for (char *arg : args) {
    args_vector.emplace_back(arg ? arg : "");
  }

  int argc_clone = static_cast<int>(args.size());

  std::vector<char *> argv_clone;
  argv_clone.reserve(args_vector.size());
  for (auto &s : args_vector) {
    argv_clone.push_back(s.data());
  }

  auto email_domains_result =
      email_helpers::load_email_domains(std::string(DEFAULT_SMTP_DOMAIN));

  smtp_config_t smtp_config_raw = {
      .host = std::string(DEFAULT_SMTP_HOST),
      .port = DEFAULT_SMTP_PORT,
      .data_directory = std::string(DEFAULT_DATA_DIRECTORY),
      .log_directory = std::string(DEFAULT_LOG_DIRECTORY),
      .certificate = std::string(DEFAULT_CERTIFICATE_FILE),
      .privatekey = std::string(DEFAULT_CERTIFICATE_FILE),
      .budget = resource_budget_t::AUTOMATIC,
      .domain = std::string(DEFAULT_SMTP_DOMAIN),
      .all_email_domains = email_domains_result,
      .proxy_support = false,
      .email_limit_size = DEFAULT_EMAIL_SIZE_LIMIT,
      .session_timeout = DEFAULT_TIMEOUT_SECONDS};
  std::unique_ptr<smtp_config_t> smtp_config =
      std::make_unique<smtp_config_t>(smtp_config_raw);

  namespace po = boost::program_options;
  po::options_description desc("Allowed options");
  // clang-format off
  desc.add_options()
    ("datadir",
      po::value<std::string>()->default_value(smtp_config->data_directory),
      "Data directory")
    ("logdir",
      po::value<std::string>()->default_value(smtp_config->log_directory),
      "Log directory")
    ("port",
      po::value<uint16_t>()->default_value(smtp_config->port),
      std::format("SMTP port to listen on (default: {})", smtp_config->port).data())
    ("timeout",
      po::value<uint32_t>()->default_value(smtp_config->session_timeout),
      std::format("Client idle timeout in seconds (default: {})", smtp_config->session_timeout).data())
    ("certificate",
      po::value<std::string>()->default_value(smtp_config->certificate),
      "X.509 certificate file")
    ("privatekey",
      po::value<std::string>()->default_value(smtp_config->privatekey),
      "X.509 private key file")
    ("proxy-support",
      po::bool_switch()->default_value(smtp_config->proxy_support),
      "Read real client IP using PROXY protocol v2")
    ("budget",
     po::value<seastar::sstring>()->required()->default_value("auto")->notifier([](const std::string& m) {
                if (m != "auto" && m != "eco") {
                    throw po::validation_error(po::validation_error::invalid_option_value,
                                               "budget", m);
                }
            }),
     "Resource budget: [auto | eco]"
     "\nauto - Automatically adapt resource allocation using optimal system configuration."
     "\neco - Use minimal resources for low-tier systems.")
    ("version",
      po::bool_switch()->default_value(false),
      "Show version and exit");
  // clang-format on

  po::variables_map povm;
  try {
    po::store(po::parse_command_line(argc_clone, argv_clone.data(), desc),
              povm);
    po::notify(povm);
  } catch (const po::error &err) {
    std::string_view err_view = err.what();
    if (err_view.find("unrecognised option '--help'") !=
        std::string_view::npos) {
      show_help = true;
    } else {
      applog.error("program options error: {}", err.what());
      return EXIT_FAILURE;
    }
  }

  seastar::app_template::seastar_options opts;
  if (!show_help && povm["budget"].as<sstring>() == "eco") {
    smtp_config->budget = resource_budget_t::ECONOMY;
    opts.smp_opts.memory.set_value("64M");
  }

  /**
   * TODO: option to change reactor backend
  opts.reactor_opts.reactor_backend.select_candidate("epoll");
  **/
  seastar::app_template app(std::move(opts));

  bool datadir_ok = false;
  bool logdir_ok = false;

  if (!show_help) {
    try {
      smtp_config->certificate = povm["certificate"].as<std::string>();
      smtp_config->privatekey = povm["privatekey"].as<std::string>();
      sstring cwd = std::filesystem::current_path().string();

      bool certificate_ok = false;
      bool privatekey_ok = false;

      try {
        smtp_config->certificate =
            std::string(std::filesystem::weakly_canonical(
                std::filesystem::path(smtp_config->certificate)));
        std::ifstream certificate_file(smtp_config->certificate);
        if (certificate_file.good()) {
          certificate_ok = true;
        } else {
          smtp_config->certificate =
              std::string(std::filesystem::weakly_canonical(
                  std::filesystem::path(cwd.c_str()) / "certificate.crt"));

          certificate_file.close();
          certificate_file.open(smtp_config->certificate);
          if (certificate_file.good()) {
            certificate_ok = true;
          } else {
            applog.error("Error reading certificate file: {}",
                         smtp_config->certificate);
          }
        }

        if (certificate_ok) {
          auto [common_name, x509_err] =
              x509_helpers::parse_x509(smtp_config->certificate);
          std::string common_name_string = std::string(common_name);
          if (x509_err == std::errc()) {
            sstring email_common_name = "user@" + common_name;
            if (email_helpers::validate_email(email_common_name)) {
              smtp_config->domain = common_name_string;
              smtp_config->all_email_domains =
                  email_helpers::load_email_domains(common_name_string);
            } else {
              applog.warn("The domain on X.509 certificate is malformed. "
                          "Auto-correcting to: {}.localdomain",
                          common_name);
              smtp_config->domain = common_name_string + ".localdomain";
              smtp_config->all_email_domains =
                  email_helpers::load_email_domains(common_name_string +
                                                    ".localdomain");
            }
          } else {
            certificate_ok = false;
            applog.error(
                "[CRITICAL!!!] Terminating service due to invalid X.509 "
                "certificate file: {}",
                smtp_config->certificate);
            return EXIT_FAILURE;
          }
        }
      } catch (std::exception_ptr ep) {
        try {
          std::rethrow_exception(ep);
        } catch (const std::exception &ex) {
          applog.error("Error on certificate file, {}", ex.what());
        }
      } catch (const std::exception &ex) {
        applog.error("Error on certificate file, {}", ex.what());
      }

      try {
        smtp_config->privatekey = std::string(std::filesystem::weakly_canonical(
            std::filesystem::path(smtp_config->privatekey)));
        std::ifstream privatekey_file(smtp_config->privatekey);
        if (privatekey_file.good()) {
          privatekey_ok = true;
        } else {
          smtp_config->privatekey =
              std::string(std::filesystem::weakly_canonical(
                  std::filesystem::path(cwd.c_str()) / "private.key"));
          privatekey_file.close();
          privatekey_file.open(smtp_config->privatekey);
          if (privatekey_file.good()) {
            privatekey_ok = true;
          } else {
            applog.error("Error reading privatekey file: {}",
                         smtp_config->privatekey);
          }
        }

        if (privatekey_ok) {
          std::errc privatekey_ec =
              x509_helpers::check_private_key(smtp_config->privatekey);
          if (privatekey_ec != std::errc()) {
            privatekey_ok = false;
            applog.error(
                "[CRITICAL!!!] Terminating service due to invalid X.509 "
                "private key file: {}",
                smtp_config->privatekey);
            return EXIT_FAILURE;
          }
        }
      } catch (std::exception_ptr ep) {
        try {
          std::rethrow_exception(ep);
        } catch (const std::exception &ex) {
          applog.error("Error on privatekey file: {}", ex.what());
        }
      } catch (const std::exception &ex) {
        applog.error("Error on privatekey file: {}", ex.what());
      }

      if (!(certificate_ok && privatekey_ok)) {
        applog.error("[CRITICAL!!!] Terminating service due to certificate and "
                     "private key file errors.");
        return EXIT_FAILURE;
      }
    } catch (const std::exception &ex) {
      applog.error("Error processing program options: {}", ex.what());
    }

    try {
      smtp_config->data_directory =
          std::string(std::filesystem::weakly_canonical(
              std::filesystem::path(povm["datadir"].as<std::string>())));

      std::errc dir_ec =
          file_helpers::checktest_directory(smtp_config->data_directory);
      if (dir_ec == std::errc()) {
        datadir_ok = true;
      } else {
        applog.error("data directory error");
      }

    } catch (const std::exception &ex) {
      applog.error("Error on data directory: {}", ex.what());
    }

    if (!datadir_ok) {
      applog.error(
          "[CRITICAL!!!] Terminating service due to data directory error.");
      return EXIT_FAILURE;
    }

    try {
      smtp_config->log_directory =
          std::string(std::filesystem::weakly_canonical(
              std::filesystem::path(povm["logdir"].as<std::string>())));

      std::errc dir_ec =
          file_helpers::checktest_directory(smtp_config->log_directory);
      if (dir_ec == std::errc()) {
        logdir_ok = true;
      } else {
        applog.error("log directory error");
      }

    } catch (const std::exception &ex) {
      applog.error("Error on log directory: {}", ex.what());
    }

    if (!logdir_ok) {
      applog.error(
          "[CRITICAL!!!] Terminating service due to log directory error.");
      return EXIT_FAILURE;
    }
  }

  app.get_options_description().add(desc);

  return app.run(
      argc, argv, [&app, &smtp_config]() mutable -> seastar::future<> {
        auto &cfg = app.configuration();

        smtp_config->port = cfg["port"].as<uint16_t>();
        smtp_config->session_timeout = cfg["timeout"].as<uint32_t>();
        smtp_config->proxy_support = cfg["proxy-support"].as<bool>();

        applog.info("Using smtp domain as: {}", smtp_config->domain);

        applog.info(
            "Email domains accepted: {}",
            email_helpers::join_email_domains(smtp_config->all_email_domains));

        auto stop_signal = std::make_shared<seastar_apps_lib::stop_signal>();

        seastar::sharded<seastar::gate> gate;
        co_await gate.start();

        seastar::sharded<seastar::abort_source> abort_sources;
        co_await abort_sources.start();

        seastar::sharded<smtp_config_t> config;
        co_await config.start(*smtp_config);

        auto shards_future =
            seastar::smp::invoke_on_all([&config, &abort_sources, &gate] {
              return serve(config.local(), abort_sources.local(), gate.local());
            });

        applog.info("server listening on 0.0.0.0 port {}", smtp_config->port);
        co_await stop_signal->wait();

        applog.info("aborting shards...");
        co_await config.stop();
        co_await abort_sources.invoke_on_all(
            [](seastar::abort_source &as) { as.request_abort(); });

        applog.info("stopping gates shards ...");
        co_await gate.invoke_on_all([](seastar::gate &g) { return g.close(); });

        applog.info("stopping abort sources...");
        co_await abort_sources.stop();
        co_await gate.stop();

        applog.info("stopping smp shards...");
        co_await std::move(shards_future);

        applog.info("server is exiting...");
        co_return;
      });
}