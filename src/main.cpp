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
#include "email_helpers.hpp"
#include "file_helpers.hpp"
#include "ip_helpers.hpp"
#include "stop_signal.hh"
#include "string_helpers.hpp"
#include "uuid_helpers.hpp"
#include "x509_helpers.hpp"

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

using namespace seastar;
using namespace string_helpers;
using namespace file_helpers;

static seastar::logger applog("smtp-server");

class dma_aligned_writer {
public:
  dma_aligned_writer(seastar::file f) : _file(std::move(f)) {
    _align = _file.disk_write_dma_alignment();
    _buffer = seastar::temporary_buffer<char>(_align);
  }

  seastar::future<> write(const char *data, size_t size) {
    size_t offset = 0;

    while (offset < size) {
      size_t space = _align - _buf_used;
      size_t to_copy = std::min(space, size - offset);

      memcpy(_buffer.get_write() + _buf_used, data + offset, to_copy);

      _buf_used += to_copy;
      offset += to_copy;

      if (_buf_used == _align) {
        co_await flush_aligned();
      }
    }
  }

  seastar::future<> flush_final() {
    if (_buf_used == 0) {
      co_return;
    }

    // pad remaining bytes
    memset(_buffer.get_write() + _buf_used, 0, _align - _buf_used);

    co_await write_all(_buffer.get(), _align);
    _buf_used = 0;
  }

private:
  seastar::file _file;
  uint64_t _pos{0};
  size_t _align{4096};

  seastar::temporary_buffer<char> _buffer;
  size_t _buf_used{0};

  seastar::future<> flush_aligned() {
    co_await write_all(_buffer.get(), _align);
    _buf_used = 0;
  }

  seastar::future<> write_all(const char *data, size_t size) {
    size_t written = 0;

    while (written < size) {
      auto n = co_await _file.dma_write(_pos, data + written, size - written);

      if (n == 0) {
        throw std::runtime_error("dma_write progress error");
      }

      written += n;
      _pos += n;
    }
  }
};

struct smtp_session {
  connected_socket cs;
  std::unique_ptr<output_stream<char>> out;
  input_stream<char> in;
  bool is_tls = false;

  smtp_session(connected_socket cs_obj)
      : cs(std::move(cs_obj)),
        out(std::make_unique<output_stream<char>>(this->cs.output())),
        in(this->cs.input()) {}

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

  future<> commit_message(seastar::lw_shared_ptr<dma_aligned_writer> logwriter,
                          uint64_t &session_state_logfile_pos,
                          seastar::sstring message) {
    if (message.empty()) {
      co_return;
    }

    session_state_logfile_pos += message.size();

    try {
      co_await logwriter->write(message.data(), message.size());
    } catch (const std::exception_ptr &ep) {
      try {
        std::rethrow_exception(ep);
      } catch (const std::system_error &e) {
        applog.error("commit log system error (errno {}): {} - what(): {}",
                     e.code().value(), e.code().message(), e.what());
      } catch (const std::exception &e) {
        applog.error("commit log exception caught: what() = {}", e.what());
      } catch (...) {
        applog.error("unknown exception while committing log");
      }
    } catch (const std::exception &ex) {
      applog.error("log commit error: {}", ex.what());
    }

    co_await out->write(message);
    co_await out->flush();

    co_return;
  }

  future<> close() {
    cs.shutdown_output();
    cs.shutdown_input();
    if (out) {
      co_await out->close();
      out = nullptr;
    }
  }
};

seastar::future<> handle_connection(
    seastar::connected_socket cs, seastar::socket_address remote,
    uint32_t timeout_seconds, seastar::gate &gate, seastar::abort_source &as,
    shared_ptr<tls::server_credentials> certs, const size_t email_size_limit,
    const seastar::sstring domain,
    seastar::lw_shared_ptr<email_domains_t> all_email_domains,
    const seastar::sstring email_domain, const seastar::sstring datadirectory,
    seastar::lw_shared_ptr<bool> proxy_support,
    seastar::lw_shared_ptr<sstring> logdirectory) {

  sstring sid_uuid = uuid_helpers::generate_v7();
  std::string_view sid_view(sid_uuid.data(), sid_uuid.size());
  sstring sid = uuid_helpers::session_uuid(sid_view);

  auto ip_info = ip_helpers::get_ip_address(remote);
  auto &ip = ip_info.ip;
  std::string sep(1, std::filesystem::path::preferred_separator);
  sstring email_real_filename = generate_email_filename();
  seastar::sstring email_filename = datadirectory + sep + email_real_filename;
  sstring log_filename = *logdirectory + sep + generate_random_logname();
  applog.info("{} new client {} connection, session logging on: {}", sid,
              remote, log_filename);

  std::unique_ptr<smtp_session> session;

  seastar::timer<> idle_timer;

  bool active = true;
  bool session_state_transaction_ok = false;
  uint64_t session_state_emailfile_pos = 0;
  uint64_t session_state_logfile_pos = 0;
  bool session_state_data_written = false;

  gate.enter();

  try {
    seastar::file log_dma_file = co_await open_file_dma(
        log_filename, open_flags::rw | open_flags::create);

    seastar::file emailfile = co_await seastar::open_file_dma(
        email_filename, seastar::open_flags::rw | seastar::open_flags::create);

    dma_aligned_writer email_writer(std::move(emailfile));
    dma_aligned_writer log_writer(std::move(log_dma_file));
    seastar::lw_shared_ptr logfile =
        seastar::make_lw_shared<dma_aligned_writer>(std::move(log_writer));

    session = std::make_unique<smtp_session>(std::move(cs));

    auto sub = as.subscribe([&]() noexcept {
      active = false;
      idle_timer.cancel();
      applog.warn("{} aborting client {} ...", sid, ip);
      try {
        session->cs.shutdown_input();
        session->cs.shutdown_output();
      } catch (...) {
        // void
      }
    });

    idle_timer.set_callback([&, ip] {
      active = false;
      applog.warn("{} client {} timeout, closing ...", sid, ip);
      try {
        session->cs.shutdown_input();
        session->cs.shutdown_output();
      } catch (...) {
        // void
      }
    });
    idle_timer.arm(std::chrono::seconds(timeout_seconds));

    constexpr size_t fixed_stream_capacity = 16;
    std::vector<char> fixed_stream(fixed_stream_capacity, '\0');
    size_t fixed_stream_size = 0;

    size_t session_state_command_index = 0;
    SMTP_COMMAND session_state_cmd{SMTP_COMMAND::UNKNOWN};

    SMTP_SESSION_STATUS session_state_status{SMTP_SESSION_STATUS::COMMAND};

    size_t session_state_rcpt_count = 0;
    size_t session_state_mailfrom_count = 0;

    bool session_state_proxy_header_read = false;
    if (*proxy_support) {
      session_state_proxy_header_read = true;
      if (session_state_proxy_header_read) {
        session_state_proxy_header_read = false;
        const auto header = co_await session->in.read_exactly(16);
        const auto *h = header.get();
        uint16_t header_length = (h[14] << 8) | h[15];
        auto body = co_await session->in.read_exactly(header_length);
        auto proxy_info =
            ip_helpers::parse_proxy_v2(header.get(), body.get(), header_length);
        if (!proxy_info) [[unlikely]] {
          applog.error("{} proxy protocol violation of client {}", sid, remote);
          active = false;
        } else {
          ip_helpers::format_ip(*proxy_info, ip_info);
          applog.info("{} detected real ip {} of client {}", sid, ip, remote);

          sstring ready =
              std::format("220 {} Service ready\r\n", domain.data());
          co_await session->commit_message(logfile, session_state_logfile_pos,
                                           std::move(ready));
        }
      }
    } else {
      sstring ready = std::format("220 {} Service ready\r\n", domain.data());
      co_await session->commit_message(logfile, session_state_logfile_pos,
                                       std::move(ready));
    }

    while (active) {
      seastar::temporary_buffer<char> buffer_stream =
          co_await session->in.read();
      if (buffer_stream.empty()) {
        break;
      }

      auto email_stream = buffer_stream.share();
      auto command_stream = buffer_stream.share();

      idle_timer.rearm(seastar::timer<>::clock::now() +
                       std::chrono::seconds(timeout_seconds));

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
        session_state_emailfile_pos += email_stream.size();
        co_await email_writer.write(email_stream.get(), email_stream.size());
        session_state_data_written = true;

        std::string_view fixed_view(fixed_stream.data(), fixed_stream_capacity);
        size_t pos = fixed_view.find(SMTP_DATA_END);

        if (pos != std::string_view::npos) {
          session_state_status = SMTP_SESSION_STATUS::COMMAND;
          session_state_transaction_ok = true;
          sstring message = "250 OK: message queued\r\n";
          co_await session->commit_message(logfile, session_state_logfile_pos,
                                           std::move(message));
        } else {
          if (session_state_emailfile_pos > DEFAULT_EMAIL_SIZE_LIMIT) {
            session_state_transaction_ok = false;

            sstring message = "552 5.3.4 Message size limit exceeded\r\n";
            co_await session->commit_message(logfile, session_state_logfile_pos,
                                             std::move(message));
            applog.error("{} client {} exceeded message size limit", sid, ip);
            active = false;
            break;
          }
        }
      } else {
        co_await logfile->write(buffer_stream.get(), buffer_stream.size());
        session_state_logfile_pos += buffer_stream.size();

        if (session_state_status == SMTP_SESSION_STATUS::COMMAND &&
            session_state_cmd != SMTP_COMMAND::UNKNOWN) {
          size_t pos = buffer_view.find(SMTP_CRLF);
          if (pos != std::string_view::npos) {

            auto [args, parse_ec] =
                email_helpers::parse_smtp_line(session_state_cmd, buffer_view);

            if (parse_ec == std::errc()) {
              std::string_view cmd_string =
                  email_helpers::smtp_command_string(session_state_cmd);
              applog.info("{} parsed command: {} {}", sid, cmd_string, args);

              switch (session_state_cmd) {
              case SMTP_COMMAND::HELO:
              case SMTP_COMMAND::EHLO: {
                session_state_mailfrom_count = 0;
                session_state_rcpt_count = 0;
                std::string_view domain_sv(domain.data(), domain.size());
                sstring message =
                    std::format("250-{} Nice to meet you, "
                                "[{}]\r\n250-8BITMIME\r\n250-SMTPUTF8\r\n250-"
                                "STARTTLS\r\n250 SIZE {}\r\n",
                                domain_sv, ip, email_size_limit);
                co_await session->commit_message(
                    logfile, session_state_logfile_pos, std::move(message));
                break;
              }
              case SMTP_COMMAND::STARTTLS: {
                sstring message = "220 Ready to start TLS\r\n";
                co_await session->commit_message(
                    logfile, session_state_logfile_pos, std::move(message));
                co_await session->upgrade_tls(certs);
                applog.info("{} session of {} upgraded to TLS", sid, ip);
                break;
              }
              case SMTP_COMMAND::MAIL: {
                auto [email, ec] = email_helpers::extract_email_address(args);

                if (ec == std::errc()) {
                  session_state_mailfrom_count++;
                  sstring message = "250 Accepted\r\n";
                  co_await session->commit_message(
                      logfile, session_state_logfile_pos, std::move(message));
                } else {
                  sstring message = "501 5.1.3 Bad email address syntax\r\n";
                  co_await session->commit_message(
                      logfile, session_state_logfile_pos, std::move(message));
                }

                break;
              }
              case SMTP_COMMAND::RCPT: {
                auto [email, ec] = email_helpers::extract_email_address(args);
                if (ec == std::errc()) {
                  sstring message = "250 Accepted\r\n";
                  co_await session->commit_message(
                      logfile, session_state_logfile_pos, std::move(message));
                  std::string_view check_email =
                      email_helpers::get_domain(email);
                  if (all_email_domains->count == 1 &&
                      compare_string_views(check_email, email_domain)) {
                    session_state_rcpt_count++;
                  } else {
                    for (size_t i = 0; i < all_email_domains->count; i++) {
                      if (compare_string_views(check_email,
                                               all_email_domains->domains[i])) {
                        session_state_rcpt_count++;
                        break;
                      }
                    }
                  }
                } else {
                  sstring message = "553 5.1.3 Bad email address syntax\r\n";
                  co_await session->commit_message(
                      logfile, session_state_logfile_pos, std::move(message));
                }

                break;
              }
              case SMTP_COMMAND::DATA: {
                if (session_state_rcpt_count >= 1 &&
                    session_state_mailfrom_count >= 1) {
                  session_state_status = SMTP_SESSION_STATUS::DATA;
                  sstring message =
                      "354 Start mail input; end with <CR><LF>.<CR><LF>\r\n";
                  co_await session->commit_message(
                      logfile, session_state_logfile_pos, std::move(message));
                } else {
                  if (session_state_rcpt_count == 0) {
                    sstring message = "554 No valid recipients\r\n";
                    co_await session->commit_message(
                        logfile, session_state_logfile_pos, std::move(message));
                  } else {
                    sstring message = "503 Bad sequence of commands\r\n";
                    co_await session->commit_message(
                        logfile, session_state_logfile_pos, std::move(message));
                  }
                  co_await session->out->flush();
                }
                break;
              }
              case SMTP_COMMAND::RSET: {
                session_state_rcpt_count = 0;
                session_state_mailfrom_count = 0;
                sstring message = "250 OK\r\n";
                co_await session->commit_message(
                    logfile, session_state_logfile_pos, std::move(message));
                break;
              }
              case SMTP_COMMAND::NOOP: {
                sstring message = "250 OK\r\n";
                co_await session->commit_message(
                    logfile, session_state_logfile_pos, std::move(message));
                break;
              }
              case SMTP_COMMAND::BDAT:
              case SMTP_COMMAND::VRFY:
              case SMTP_COMMAND::SEND:
              case SMTP_COMMAND::SOML:
              case SMTP_COMMAND::AUTH: {
                sstring message = "502 Command not implemented\r\n";
                co_await session->commit_message(
                    logfile, session_state_logfile_pos, std::move(message));
                break;
              }
              case SMTP_COMMAND::QUIT: {
                sstring message = "221 Bye\r\n";
                co_await session->commit_message(
                    logfile, session_state_logfile_pos, std::move(message));
                active = false;
                break;
              }
              case SMTP_COMMAND::UNKNOWN:
              default:
                sstring message = "500 Syntax error\r\n";
                co_await session->commit_message(
                    logfile, session_state_logfile_pos, std::move(message));
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
          applog.warn("{} unknown command: {}", sid, buffer_view);
          sstring message = "500 Syntax error, command unrecognized\r\n";
          co_await session->commit_message(logfile, session_state_logfile_pos,
                                           std::move(message));
        }

        if (session_state_logfile_pos > SMTP_COMMAND_BUFFER_SIZE_LIMIT) {

          applog.error("{} client {} exceeded command buffer limit", sid, ip);
          sstring message = "552 5.3.4 Message size limit exceeded\r\n";
          co_await session->commit_message(logfile, session_state_logfile_pos,
                                           std::move(message));
        }
      }
    }

    // flush and commit DMA files
    co_await seastar::when_all_succeed(logfile->flush_final(),
                                       email_writer.flush_final());

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

  idle_timer.cancel();

  try {
    session->cs.shutdown_input();
    session->cs.shutdown_output();
    co_await session->in.close();
  } catch (...) {
    // void
  }
  try {
    co_await session->close();
  } catch (...) {
    // void
  }

  if (!session_state_data_written) {
    auto ec = file_helpers::delete_file(email_filename);
    if (ec == std::errc()) {
      applog.warn("{} removed empty incomplete email {}", sid, email_filename);
    } else {
      applog.error("{} failed to remove empty incomplete email {}", sid,
                   email_filename);
    }
  }

  applog.info("{} client [{}] {} connection finished.", sid, ip, remote);

  gate.leave();

  co_return;
}

seastar::future<>
serve(uint16_t port, seastar::abort_source &as, seastar::gate &gate,
      uint32_t timeout_seconds, const size_t email_size_limit,
      const seastar::sstring domain, const seastar::sstring certificate,
      const seastar::sstring privatekey,
      seastar::lw_shared_ptr<email_domains_t> all_email_domains,
      const seastar::sstring email_domain, const seastar::sstring datadirectory,
      seastar::lw_shared_ptr<bool> proxy_support,
      seastar::lw_shared_ptr<sstring> logdirectory) {

  applog.trace("Loading X.509 certificates {}, {} ...", certificate,
               privatekey);
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

  // TODO: Load concurrent connection limit from configuration
  seastar::semaphore connect_semaphore(24);

  while (!as.abort_requested()) {
    try {
      auto ar = co_await ss.accept();

      co_await connect_semaphore.wait();

      auto addr = ar.remote_address;
      connection_count++;
      (void)handle_connection(std::move(ar.connection), addr, timeout_seconds,
                              gate, as, certs, email_size_limit, domain,
                              all_email_domains, email_domain, datadirectory,
                              proxy_support, logdirectory)
          .handle_exception([=](std::exception_ptr ep) {
            try {
              std::rethrow_exception(ep);
            } catch (const std::exception &ex) {
              applog.error("Error closing connections: {}", ex.what());
            }
          })
          .finally([&gate, &connection_count, &connect_semaphore] {
            connect_semaphore.signal();
            connection_count--;
            applog.trace("finally closing connections...");
          });
    } catch (const seastar::abort_requested_exception &) {
      break;
    } catch (const std::exception &ex) {
      if (!as.abort_requested()) {
        applog.error("connection accept failed: {}", ex.what());
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

  // TODO: Load performance tuning from configuration
  seastar::app_template::seastar_options opts;
  opts.smp_opts.memory.set_value("64M");

  seastar::app_template app(std::move(opts));

  uint16_t port = 2525;
  uint16_t default_timeout_seconds = 120;

  namespace po = boost::program_options;
  po::options_description desc("Allowed options");
  // clang-format off
  desc.add_options()
    ("datadir",
      po::value<seastar::sstring>()->default_value("/var/spool/smtp"),
      "Data directory")
    ("logdir",
      po::value<seastar::sstring>()->default_value("/var/log/smtp"),
      "Log directory")
    ("port",
      po::value<uint16_t>()->default_value(port),
      std::format("SMTP port to listen on (default: {})", port).data())
    ("timeout",
      po::value<uint32_t>()->default_value(default_timeout_seconds),
      std::format("Client idle timeout in seconds (default: {})", default_timeout_seconds).data())
    ("certificate",
      po::value<seastar::sstring>()->default_value("/etc/ssl/private/mail/certificate.crt"),
      "X.509 certificate file")
    ("privatekey",
      po::value<seastar::sstring>()->default_value("/etc/ssl/private/mail/private.key"),
      "X.509 private key file")
    ("proxy-support",
      po::bool_switch()->default_value(false),
      "Read real client IP using PROXY protocol v2")
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

  // TODO: Load domain configuration
  sstring domain = "localhost.localdomain";
  sstring email_domain = "localhost.localdomain";
  std::string datadirectory = "/var/spool/smtp";
  std::string logdirectory = "/var/log/smtp";
  bool datadir_ok = false;
  bool logdir_ok = false;

  std::string certificate = "certificate.crt";
  std::string privatekey = "private.key";

  if (!show_help) {
    try {

      sstring certificate_file_path = povm["certificate"].as<sstring>();
      sstring privatekey_file_path = povm["privatekey"].as<sstring>();
      sstring cwd = std::filesystem::current_path().string();

      bool certificate_ok = false;
      bool privatekey_ok = false;

      try {
        certificate_file_path = std::string(std::filesystem::weakly_canonical(
            std::filesystem::path(certificate_file_path)));
        std::ifstream certificate_file(certificate_file_path);
        if (certificate_file.good()) {
          certificate_ok = true;
        } else {
          certificate_file_path = std::string(std::filesystem::weakly_canonical(
              std::filesystem::path(cwd.c_str()) / "certificate.crt"));

          certificate_file.close();
          certificate_file.open(certificate_file_path);
          if (certificate_file.good()) {
            certificate_ok = true;
          } else {
            applog.error("Error reading certificate file: {}",
                         certificate_file_path);
          }
        }

        if (certificate_ok) {
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
            certificate_ok = false;
            applog.error(
                "[CRITICAL!!!] Terminating service due to invalid X.509 "
                "certificate file: {}",
                certificate_file_path);
            return EXIT_FAILURE;
          }
        }

        certificate = certificate_file_path;
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
        privatekey_file_path = std::string(std::filesystem::weakly_canonical(
            std::filesystem::path(privatekey_file_path)));
        std::ifstream privatekey_file(privatekey_file_path);
        if (privatekey_file.good()) {
          privatekey_ok = true;
        } else {
          privatekey_file_path = std::string(std::filesystem::weakly_canonical(
              std::filesystem::path(cwd.c_str()) / "private.key"));
          privatekey_file.close();
          privatekey_file.open(privatekey_file_path);
          if (privatekey_file.good()) {
            privatekey_ok = true;
          } else {
            applog.error("Error reading privatekey file: {}",
                         privatekey_file_path);
          }
        }

        if (privatekey_ok) {
          std::errc privatekey_ec =
              x509_helpers::check_private_key(privatekey_file_path);
          if (privatekey_ec != std::errc()) {
            privatekey_ok = false;
            applog.error(
                "[CRITICAL!!!] Terminating service due to invalid X.509 "
                "private key file: {}",
                privatekey_file_path);
            return EXIT_FAILURE;
          }
        }

        privatekey = privatekey_file_path;
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
      datadirectory = std::string(std::filesystem::weakly_canonical(
          std::filesystem::path(povm["datadir"].as<sstring>())));

      std::errc dir_ec = file_helpers::checktest_directory(datadirectory);
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
      logdirectory = std::string(std::filesystem::weakly_canonical(
          std::filesystem::path(povm["logdir"].as<sstring>())));

      std::errc dir_ec = file_helpers::checktest_directory(logdirectory);
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
      argc, argv,
      [&app, domain_data = std::move(domain),
       email_data = std::move(email_domain), datadir = std::move(datadirectory),
       logdir = std::move(logdirectory),
       certificate_file_path = std::move(certificate),
       privatekey_file_path = std::move(privatekey)]() mutable -> seastar::future<> {
        auto &cfg = app.configuration();

        size_t email_size_limit = DEFAULT_EMAIL_SIZE_LIMIT;
        uint16_t port = cfg["port"].as<uint16_t>();
        uint32_t timeout_seconds = cfg["timeout"].as<uint32_t>();

        sstring domain = sstring(domain_data);
        sstring email_domain = sstring(email_data);

        sstring datadirectory = sstring(datadir);
        sstring log_directory = sstring(logdir);

        sstring certificate = sstring(certificate_file_path);
        sstring privatekey = sstring(privatekey_file_path);

        applog.info("Using smtp domain as: {}", domain);

        auto email_domains_result =
            email_helpers::load_email_domains(email_domain);
        applog.info("Email domains accepted: {}",
                    email_helpers::join_email_domains(email_domains_result));

        auto stop_signal = std::make_shared<seastar_apps_lib::stop_signal>();

        seastar::sharded<seastar::gate> gate;
        co_await gate.start();

        seastar::sharded<seastar::abort_source> abort_sources;
        co_await abort_sources.start();

        seastar::lw_shared_ptr<email_domains_t> all_email_domains =
            seastar::make_lw_shared<email_domains_t>(email_domains_result);

        seastar::lw_shared_ptr<bool> proxy_support =
            seastar::make_lw_shared<bool>(cfg["proxy-support"].as<bool>());

        seastar::lw_shared_ptr<sstring> logdirectory =
            seastar::make_lw_shared<sstring>(std::move(log_directory));

        auto shards_future = seastar::smp::invoke_on_all(
            [port, &abort_sources, &gate, timeout_seconds,
             email_size_limit = email_size_limit, domain = sstring(domain),
             datadirectory, logdirectory, certificate, privatekey, email_domain,
             all_email_domains, proxy_support] {
              return serve(port, abort_sources.local(), gate.local(),
                           timeout_seconds, email_size_limit, domain,
                           certificate, privatekey, all_email_domains,
                           email_domain, datadirectory, proxy_support,
                           logdirectory);
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