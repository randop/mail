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

#include <seastar/core/app-template.hh>
#include <seastar/core/coroutine.hh>
#include <seastar/core/file.hh>
#include <seastar/core/fstream.hh>
#include <seastar/core/future-util.hh>
#include <seastar/core/future.hh>
#include <seastar/core/gate.hh>
#include <seastar/core/queue.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/seastar.hh>
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
#include <arpa/inet.h>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <format>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "email_helpers.hpp"
#include "file_helpers.hpp"
#include "stop_signal.hh"
#include "uuid_helpers.hpp"
#include "x509_helpers.hpp"

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

using namespace seastar;

static seastar::logger applog("smtp-server");

struct ip_result {
  char ip[INET6_ADDRSTRLEN];
  std::errc ec;
};

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

struct smtp_session {
  connected_socket cs;
  std::unique_ptr<output_stream<char>> out;
  input_stream<char> in;
  std::unique_ptr<output_stream<char>> logfile;
  bool is_tls = false;

  smtp_session(connected_socket cs_obj, output_stream<char> logfile_obj)
      : cs(std::move(cs_obj)),
        out(std::make_unique<output_stream<char>>(this->cs.output())),
        in(this->cs.input()),
        logfile(std::make_unique<output_stream<char>>(std::move(logfile_obj))) {
  }

  future<> send(std::string_view msg) {
    if (logfile) {
      co_await logfile->write(msg.data(), msg.size());
    }
    if (out) {
      co_await out->write(msg.data(), msg.size());
      co_await out->flush();
    }
  }

  future<> upgrade_tls(shared_ptr<tls::server_credentials> certs) {
    if (out) {
      co_await out->flush();
      /*** IMPORTANT: out.release() to avoid TLS upgrade issues ***/
      (void)out.release();
    }
    in = {};

    cs = co_await tls::wrap_server(certs, std::move(cs));
    out = std::make_unique<output_stream<char>>(cs.output());
    in = cs.input();
    is_tls = true;
  }

  future<> close() {
    cs.shutdown_output();
    cs.shutdown_input();
    if (out) {
      co_await out->close();
      out = nullptr;
    }
    if (logfile) {
      co_await logfile->close();
      logfile = nullptr;
    }
  }
};

/// @warning Passing `nullptr` for @p a results in undefined behavior.
constexpr bool compare_strings_ab(const char *a, const std::string &b) {
  if (!a) {
    return false;
  }

  size_t i = 0;

  auto to_lower = [](unsigned char c) constexpr {
    return static_cast<char>(std::tolower(c));
  };

  size_t b_size = b.size();

  while (a[i] != '\0' && i < b_size) {
    if (to_lower(static_cast<unsigned char>(a[i])) !=
        to_lower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
    if ((i + 1) > b_size) {
      break;
    }
    ++i;
  }

  return i == b_size;
}

constexpr bool compare_strings_view(const std::string_view &a,
                                    const std::string_view &b) {
  if (a.size() == 0 || b.size() == 0) {
    return false;
  }

  size_t i = 0;

  auto to_lower = [](unsigned char c) constexpr {
    return static_cast<char>(std::tolower(c));
  };

  size_t b_size = b.size();

  while (a[i] != '\0' && i < b_size) {
    if (to_lower(static_cast<unsigned char>(a[i])) !=
        to_lower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
    if ((i + 1) > b_size) {
      break;
    }
    ++i;
  }

  return i == b_size;
}

seastar::future<> handle_connection(
    seastar::connected_socket cs, seastar::socket_address remote,
    uint32_t timeout_seconds, seastar::gate &gate, seastar::abort_source &as,
    shared_ptr<tls::server_credentials> certs, const size_t email_size_limit,
    const seastar::sstring domain, const email_domains_t email_domains) {
  auto [ip, ec] = get_ip_address(remote);
  seastar::sstring email_filename = file_helpers::generate_email_filename();
  applog.info("New client {} connection, session: {}", ip, email_filename);

  std::unique_ptr<smtp_session> session;

  seastar::timer<> idle_timer;

  seastar::sstring cmd_buffer;
  std::string_view cmd_view;
  size_t cmd_pos = 0;
  size_t cmd_start_index = 0;
  size_t crlf_pos = 0;
  bool in_command = true;
  bool in_crlf = false;
  bool in_cmd_boundary = false;
  constexpr int ZERO = 0;

  seastar::sstring data_buffer;
  bool in_data = false;
  size_t data_pos = 0;
  size_t data_size = 0;

  bool active = true;
  bool is_data_ended = false;

  uint64_t email_pos = 0;

  uint8_t ok_rcpt_count = 0;
  uint8_t ok_mailfrom_count = 0;

  gate.enter();

  try {
    auto log_filename = file_helpers::generate_random_logname();

    seastar::file logfile = co_await open_file_dma(
        log_filename,
        open_flags::rw | open_flags::create | open_flags::truncate);

    seastar::file emailfile = co_await seastar::open_file_dma(
        email_filename, seastar::open_flags::rw | seastar::open_flags::create);

    uint64_t emailfile_align = emailfile.disk_write_dma_alignment();
    std::string email_buffer;
    email_buffer.reserve(emailfile_align * 2);

    session = std::make_unique<smtp_session>(
        std::move(cs), co_await make_file_output_stream(std::move(logfile)));

    auto sub = as.subscribe([&]() noexcept {
      active = false;
      idle_timer.cancel();
      applog.warn("aborting client {} ...", remote);
      try {
        session->cs.shutdown_input();
        session->cs.shutdown_output();
      } catch (...) {
        // void
      }
    });

    idle_timer.set_callback([&, remote] {
      active = false;
      applog.warn("client {} timeout, closing ...", remote);
      try {
        session->cs.shutdown_input();
        session->cs.shutdown_output();
      } catch (...) {
        // void
      }
    });
    idle_timer.arm(std::chrono::seconds(timeout_seconds));

    co_await session->send(seastar::sstring(
        std::format("220 {} Service ready\r\n", domain.data())));

    while (active) {
      temporary_buffer<char> buf = co_await session->in.read();
      if (buf.empty()) {
        break;
      }

      idle_timer.rearm(seastar::timer<>::clock::now() +
                       std::chrono::seconds(timeout_seconds));

      if (!in_data) {
        cmd_buffer.append(buf.get(), buf.size());
        if (session->logfile) {
          co_await session->logfile->write(buf.get(), buf.size());
        }
      }

      if (in_data) {
        applog.info("IN DATA MODE...");
        data_size += buf.size();
        if (data_size > email_size_limit) {
          co_await session->send("552 5.3.4 Message size limit exceeded\r\n");
          active = false;
          break;
        }

        data_buffer.append(buf.get(), buf.size());
        if ((data_pos + 7) <= data_buffer.size()) {
          std::string_view data_terminator = data_buffer.substr(data_pos);
          if (data_terminator.find("\r\n\r\n.\r\n") != std::string_view::npos) {
            in_data = false;
            in_command = true;
            is_data_ended = true;
            co_await session->send("250 OK: message queued\r\n");
          }
          data_pos = data_buffer.size();
        }

        if (!is_data_ended) {
          if (data_buffer.size() > 14 &&
              ((data_pos - 14) < data_buffer.size())) {
            std::string_view data_terminator =
                data_buffer.substr(data_pos - 14);
            if (data_terminator.find("\r\n\r\n.\r\n") !=
                std::string_view::npos) {
              in_data = false;
              in_command = true;
              is_data_ended = true;
              co_await session->send("250 OK: message queued\r\n");
            }
          }
        }

        email_buffer.append(buf.get(), buf.size());
        if (email_buffer.size() >= emailfile_align) {
          applog.info("DMA_WRITE: email content <{}>", email_filename);
          size_t to_write = email_buffer.size() & ~(emailfile_align - 1);

          seastar::temporary_buffer<char> out(to_write);
          memcpy(out.get_write(), email_buffer.data(), to_write);

          co_await emailfile.dma_write(email_pos, out.get(), to_write);
          email_pos += to_write;

          // remove written portion (O(n), acceptable for <=512KB)
          email_buffer = email_buffer.substr(to_write);
        }

      } else {
        if (in_command) {
          in_crlf = false;
          in_cmd_boundary = false;
          in_data = false;
          if ((cmd_pos + 4) <= cmd_buffer.size()) {
            auto *p = cmd_buffer.data() + cmd_pos;
            if (compare_strings_ab(p, "EHLO") ||
                compare_strings_ab(p, "HELO")) {
              applog.info("EHLO / HELO found at {}", cmd_pos);
              cmd_start_index = cmd_pos;
              in_cmd_boundary = true;
              cmd_pos += 4;
            } else if (compare_strings_ab(p, "QUIT")) {
              applog.info("QUIT found at {}", cmd_pos);
              cmd_start_index = cmd_pos;
              in_cmd_boundary = true;
              cmd_pos += 4;
            } else if (compare_strings_ab(p, "AUTH")) {
              applog.info("AUTH found at {}", cmd_pos);
              cmd_start_index = cmd_pos;
              in_cmd_boundary = true;
              cmd_pos += 4;
            } else if (compare_strings_ab(p, "DATA")) {
              applog.info("DATA found at {}", cmd_pos);
              cmd_start_index = cmd_pos;
              in_command = false;
              in_data = true;
              in_cmd_boundary = true;
              cmd_pos += 4;
            }
          }
          if ((cmd_pos + 8) <= cmd_buffer.size()) {
            auto *p = cmd_buffer.data() + cmd_pos;
            if (compare_strings_ab(p, "RCPT TO:")) {
              applog.info("RCPT TO found at {}", cmd_pos);
              cmd_start_index = cmd_pos;
              in_cmd_boundary = true;
              cmd_pos += 8;
            }
            if (compare_strings_ab(p, "STARTTLS")) {
              applog.info("STARTTLS found at {}", cmd_pos);
              cmd_start_index = cmd_pos;
              in_cmd_boundary = true;
              cmd_pos += 8;
            }
          }
          if ((cmd_pos + 10) <= cmd_buffer.size()) {
            auto *p = cmd_buffer.data() + cmd_pos;
            if (compare_strings_ab(p, "MAIL FROM:")) {
              applog.info("MAIL FROM found at {}", cmd_pos);
              cmd_start_index = cmd_pos;
              in_cmd_boundary = true;
              cmd_pos += 10;
            }
          }
        }

        crlf_pos = cmd_pos;

        if (in_cmd_boundary) {
          while (crlf_pos + 1 < cmd_buffer.size()) {
            if (cmd_buffer[crlf_pos] == '\r' &&
                cmd_buffer[crlf_pos + 1] == '\n') {
              applog.info("<CRLF> found at {}", crlf_pos);
              crlf_pos += 2;
              in_command = true;
              in_crlf = true;

              size_t p_size = (crlf_pos - cmd_start_index) - 1;
              // guard against negative value
              if ((crlf_pos - cmd_start_index - 1) < ZERO) {
                p_size = 0;
              }
              cmd_view =
                  std::string_view(cmd_buffer.data() + cmd_start_index, p_size);

              break;
            }
            ++crlf_pos;
          }
        } else {
          // clear
          cmd_view = std::string_view(cmd_buffer.data(), 0);
          applog.info("clear cmd_view: {}", cmd_view);
        }

        if (in_command && in_crlf) {
          cmd_pos = crlf_pos;
          cmd_start_index = cmd_pos;
          applog.info("command: {}", cmd_view);

          if (cmd_view.starts_with("EHLO ") || cmd_view.starts_with("HELO ")) {
            ok_rcpt_count = 0;
            ok_mailfrom_count = 0;

            std::string_view domain_sv(domain.data(), domain.size());

            co_await session->out->write(seastar::sstring(std::format(
                "250-{} Nice to meet you, [{}]\r\n", domain_sv, ip)));
            co_await session->logfile->write(seastar::sstring(std::format(
                "250-{} Nice to meet you, [{}]\r\n", domain_sv, ip)));
            co_await session->out->write("250-8BITMIME\r\n");
            co_await session->logfile->write("250-8BITMIME\r\n");
            co_await session->out->write("250-SMTPUTF8\r\n");
            co_await session->logfile->write("250-SMTPUTF8\r\n");
            co_await session->out->write("250-STARTTLS\r\n");
            co_await session->logfile->write("250-STARTTLS\r\n");
            co_await session->out->write(seastar::sstring(
                std::format("250 SIZE {}\r\n", email_size_limit)));
            co_await session->logfile->write(seastar::sstring(
                std::format("250 SIZE {}\r\n", email_size_limit)));
            co_await session->out->flush();
          } else if (cmd_view.starts_with("RCPT TO:")) {
            auto [email, ec] = email_helpers::extract_email_address(cmd_view);
            if (ec == std::errc()) {
              co_await session->send("250 Accepted\r\n");

              std::string_view check_email = email.substr(email.find("@") + 1);
              for (size_t i = 0; i < email_domains.count; i++) {
                if (compare_strings_view(check_email,
                                         email_domains.domains[i])) {
                  ok_rcpt_count++;
                  break;
                }
              }

            } else {
              co_await session->send("553 5.1.3 Bad email address syntax\r\n");
            }
          } else if (cmd_view.starts_with("MAIL FROM:")) {
            auto [email, ec] = email_helpers::extract_email_address(cmd_view);
            if (ec == std::errc()) {
              co_await session->send("250 Accepted\r\n");
              ok_mailfrom_count++;
            } else {
              co_await session->send("501 5.1.3 Bad email address syntax\r\n");
            }
          } else if (cmd_view.starts_with("STARTTLS")) {
            co_await session->send("220 Ready to start TLS\r\n");
            co_await session->upgrade_tls(certs);
            applog.info("Session {} upgraded to TLS", remote);
          } else if (cmd_view.starts_with("DATA")) {
            if (ok_rcpt_count >= 1 && ok_mailfrom_count >= 1) {
              in_data = true;
              in_command = false;
              co_await session->send(
                  "354 Start mail input; end with <CR><LF>.<CR><LF>\r\n");
            } else {
              in_command = true;
              in_data = false;
              if (ok_rcpt_count == 0) {
                co_await session->send("554 No valid recipients\r\n");
              } else {
                co_await session->send("503 Bad sequence of commands\r\n");
              }
            }
          } else if (cmd_view.starts_with("AUTH")) {
            co_await session->send("502 Command not implemented\r\n");
          } else if (cmd_view.starts_with("QUIT")) {
            co_await session->send("221 Bye\r\n");
          } else {
            co_await session->send("500 Syntax error\r\n");
          }

          in_crlf = false;

          // clear
          cmd_view = std::string_view(cmd_buffer.data(), 0);
        }
      }
    }

    // final tail (pad once)
    if (!email_buffer.empty()) {
      applog.info("DMA_WRITE: email remnant <{}>", email_filename);
      size_t padded =
          (email_buffer.size() + emailfile_align - 1) & ~(emailfile_align - 1);

      seastar::temporary_buffer<char> out(padded);

      memcpy(out.get_write(), email_buffer.data(), email_buffer.size());
      memset(out.get_write() + email_buffer.size(), 0,
             padded - email_buffer.size());

      co_await emailfile.dma_write(email_pos, out.get(), padded);
      email_pos += padded;
    }

    co_await emailfile.flush();

    applog.info("Writing client {} logs on {} and email file: {}", remote,
                log_filename, email_filename);
  } catch (const seastar::timed_out_error &err) {
    applog.info("Client {} idle timeout", remote);
  } catch (const std::exception &ex) {
    applog.warn("Connection {} error: {}", remote, ex.what());
  }

  idle_timer.cancel();

  try {
    co_await session->in.close();
  } catch (...) {
    // void
  }
  try {
    co_await session->close();
  } catch (...) {
    // void
  }

  applog.info("Client {} connection finished", remote);

  gate.leave();

  co_return;
}

seastar::future<>
serve(uint16_t port, seastar::abort_source &as, seastar::gate &gate,
      uint32_t timeout_seconds, const size_t email_size_limit,
      const seastar::sstring domain, const seastar::sstring certificate,
      const seastar::sstring privatekey, const email_domains_t email_domains) {
  applog.info("Loading X.509 certificates...");
  auto certs = make_shared<tls::server_credentials>();
  co_await certs->set_x509_key_file(certificate, privatekey,
                                    tls::x509_crt_format::PEM);
  seastar::listen_options opts;
  opts.reuse_address = true;
  opts.lba =
      seastar::server_socket::load_balancing_algorithm::connection_distribution;

  auto ss = seastar::listen(seastar::make_ipv4_address({port}), opts);

  applog.info("shard {} listening on 0.0.0.0:{}", seastar::this_shard_id(),
              port);
  seastar::timer<> timer;
  uint64_t connection_count = 0;
  bool stats = false;
  timer.set_callback([&connection_count, &timer, &stats] {
    if (stats) {
      applog.info("Active connections on shard {}: {}",
                  seastar::this_shard_id(), connection_count);
    }
    timer.arm(std::chrono::seconds(30));
  });
  timer.arm(std::chrono::seconds(30));

  auto sub_opt = as.subscribe([&]() noexcept {
    timer.cancel();
    ss.abort_accept();
  });

  while (!as.abort_requested()) {
    try {
      auto ar = co_await ss.accept();
      auto addr = ar.remote_address;
      connection_count++;
      (void)handle_connection(std::move(ar.connection), addr, timeout_seconds,
                              gate, as, certs, email_size_limit, domain,
                              email_domains)
          .handle_exception([=](std::exception_ptr ep) {
            try {
              std::rethrow_exception(ep);
            } catch (const std::exception &ex) {
              applog.error("Error closing connections: {}", ex.what());
            }
          })
          .finally([&gate, &connection_count] {
            connection_count--;
            applog.info("Finally closing connections...");
          });
    } catch (const seastar::abort_requested_exception &) {
      break;
    } catch (const std::exception &ex) {
      if (!as.abort_requested()) {
        applog.error("Accept failed: {}", ex.what());
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

  const std::span<char *const> args{argv, static_cast<std::size_t>(argc)};

  seastar::app_template::seastar_options opts;
  opts.smp_opts.memory.set_value("64M");

  seastar::app_template app(std::move(opts));

  uint16_t port = 2525;
  uint16_t default_timeout_seconds = 30;

  // clang-format off
  namespace po = boost::program_options;
  app.add_options()
    ("datadir,d",
      po::value<seastar::sstring>()->default_value("/var/spool/smtp"),
      "Data directory")
    ("port,p",
      po::value<uint16_t>()->default_value(port),
      std::format("SMTP port to listen on (default: {})", port).data())
    ("timeout,t",
      po::value<uint32_t>()->default_value(default_timeout_seconds),
      std::format("Client idle timeout in seconds (default: {})", default_timeout_seconds).data())
    ("certificate,c",
      po::value<seastar::sstring>()->default_value("/etc/ssl/private/mail/certificate.crt"),
      "X.509 certificate file")
    ("privatekey,k",
      po::value<seastar::sstring>()->default_value("/etc/ssl/private/mail/private.key"),
      "X.509 private key file")
    ("auto-x509-domain,x",
      po::bool_switch()->default_value(true),
      "Loads domain using X.509 common name value from certificate file automatically")
    ("version",
      po::bool_switch()->default_value(false),
      "Show version and exit");
  // clang-format on

  return app.run(argc, argv, [&app]() -> seastar::future<> {
    applog.info("mail version {}", PROJECT_VERSION);

    auto &cfg = app.configuration();

    size_t email_size_limit = 524288; // 512KB
    uint16_t port = cfg["port"].as<uint16_t>();
    uint32_t timeout_seconds = cfg["timeout"].as<uint32_t>();
    sstring certificate_file_path = cfg["certificate"].as<sstring>();
    sstring privatekey_file_path = cfg["privatekey"].as<sstring>();
    sstring cwd = std::filesystem::current_path().string();

    bool certificate_ok = false;
    bool privatekey_ok = false;

    try {
      std::ifstream certificate_file(certificate_file_path);
      if (!certificate_file.good()) {
        certificate_file_path = std::string(std::filesystem::weakly_canonical(
            std::filesystem::path(cwd.c_str()) / "certificate.crt"));
      }
      if (co_await seastar::file_accessible(certificate_file_path,
                                            seastar::access_flags::read)) {
        certificate_ok = true;
      } else {
        applog.error("Error reading certificate file: {}",
                     certificate_file_path);
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
      std::ifstream privatekey_file(privatekey_file_path);
      if (!privatekey_file.good()) {
        privatekey_file_path = std::string(std::filesystem::weakly_canonical(
            std::filesystem::path(cwd.c_str()) / "private.key"));
      }
      if (co_await seastar::file_accessible(privatekey_file_path,
                                            seastar::access_flags::read)) {
        privatekey_ok = true;
      } else {
        applog.error("Error reading privatekey file: {}", privatekey_file_path);
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
      co_return;
    }

    // TODO: Load domain configuration
    sstring domain = "localhost.localdomain";
    sstring email_domain = "localhost.localdomain";

    auto [common_name, x509_err] =
        x509_helpers::parse_x509(certificate_file_path);

    if (x509_err == std::errc()) {
      sstring email_common_name = "user@" + common_name;
      if (email_helpers::validate_email(email_common_name)) {
        domain = common_name;
        email_domain = common_name;
      } else {
        applog.warn("The domain on X.509 certificate is malformed. "
                    "Auto-correcting to: {}.localdomain",
                    common_name);
        domain = common_name + ".localdomain";
        email_domain = domain;
      }
    } else {
      applog.error("[CRITICAL!!!] Terminating service due to invalid X.509 "
                   "certificate file: {}",
                   certificate_file_path);
      co_return;
    }

    std::errc privatekey_ec =
        x509_helpers::check_private_key(privatekey_file_path);
    if (privatekey_ec != std::errc()) {
      applog.error("[CRITICAL!!!] Terminating service due to invalid X.509 "
                   "private key file: {}",
                   privatekey_file_path);
      co_return;
    }

    applog.info("Using smtp domain as: {}", domain);

    auto email_domains_result = email_helpers::load_email_domains(email_domain);
    applog.info("Email domains accepted: {}",
                email_helpers::join_email_domains(email_domains_result));

    auto stop_signal = std::make_shared<seastar_apps_lib::stop_signal>();

    seastar::sharded<seastar::gate> gate;
    co_await gate.start();

    seastar::sharded<seastar::abort_source> abort_sources;
    co_await abort_sources.start();

    auto shards_future = seastar::smp::invoke_on_all(
        [port, &abort_sources, &gate, timeout_seconds,
         email_size_limit = email_size_limit, domain = sstring(domain),
         certificate = sstring(certificate_file_path),
         privatekey = sstring(privatekey_file_path),
         email_domains = email_domains_result] {
          return serve(port, abort_sources.local(), gate.local(),
                       timeout_seconds, email_size_limit, domain, certificate,
                       privatekey, email_domains);
        });

    applog.info("server listening on 0.0.0.0 port {}", port);
    co_await stop_signal->wait();

    applog.info("aborting shards...");
    co_await abort_sources.invoke_on_all(
        [](seastar::abort_source &as) { as.request_abort(); });

    applog.info("stopping smp shards...");
    co_await std::move(shards_future);

    applog.info("stopping gates shards ...");
    co_await gate.invoke_on_all([](seastar::gate &g) { return g.close(); });

    applog.info("stopping abort sources...");
    co_await abort_sources.stop();
    co_await gate.stop();

    applog.info("server is exiting...");
    co_return;
  });
}