/*****************************************************************************
mail
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
#include <seastar/core/reactor.hh>
#include <seastar/core/seastar.hh>
#include <seastar/core/sharded.hh>
#include <seastar/core/shared_ptr.hh>
#include <seastar/core/timer-set.hh>
#include <seastar/net/api.hh>
#include <seastar/net/tls.hh>
#include <seastar/util/closeable.hh>
#include <seastar/util/log.hh>
#include <seastar/util/tmp_file.hh>

#include "stop_signal.hh"

#include <arpa/inet.h>
#include <cctype>
#include <chrono>
#include <cstring>
#include <exception>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <utility>

using namespace seastar;

static seastar::logger applog("smtp-server");

struct crlf_result {
  // line segment exclusive of \r\n
  std::string_view prefix;
  bool found = false;
  size_t bytes_consumed = 0;
};

struct data_delimeter_result {
  bool found = false;
  size_t bytes_consumed = 0;
};

struct ip_result {
  char ip[INET6_ADDRSTRLEN];
  std::errc ec;
};

data_delimeter_result find_data_delimeter(
    const std::vector<seastar::temporary_buffer<char>> &chunks) noexcept {
  if (chunks.empty()) {
    return {};
  }

  constexpr std::string_view delim = "\r\n\r\n\r\n.\r\n";
  constexpr size_t dlen = delim.size();

  size_t chunk_offset = 0;
  int state = 0;

  for (const auto &buf : chunks) {
    if (buf.empty())
      continue;

    const char *p = buf.get();
    const char *const end = p + buf.size();

    while (p < end) {
      const char c = *p++;

      if (c == delim[state]) {
        ++state;
        if (state == dlen) {
          return {true, chunk_offset + (p - buf.get())};
        }
      } else {
        state = (c == delim[0]) ? 1 : 0;
      }
    }

    chunk_offset += buf.size();
  }

  return {false, chunk_offset};
}

crlf_result find_first_crlf(
    const std::vector<seastar::temporary_buffer<char>> &chunks) noexcept {
  if (chunks.empty()) {
    return {};
  }

  size_t chunk_offset = 0;

  for (size_t i = 0; i < chunks.size(); ++i) {
    const auto &buf = chunks[i];
    if (buf.empty())
      continue;

    const char *data = buf.get();
    const size_t len = buf.size();

    for (size_t j = 0; j < len; ++j) {
      if (data[j] == '\n') {
        if (j > 0 && data[j - 1] == '\r') {
          std::string_view prefix(data, j - 1);
          return {prefix, true, chunk_offset + j + 1};
        }
      }
    }

    if (len > 0 && data[len - 1] == '\r') {
      if (i + 1 < chunks.size()) {
        const auto &next_buf = chunks[i + 1];
        if (!next_buf.empty() && next_buf.get()[0] == '\n') {
          std::string_view prefix(data, len - 1);
          return {prefix, true, chunk_offset + len + 1};
        }
      }
    }

    chunk_offset += len;
  }

  return {{}, false, chunk_offset};
}

struct uuidv7 {
  static seastar::sstring generate() {
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      lowres_system_clock::now().time_since_epoch())
                      .count();
    thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist;

    uint8_t b[16];
    uint64_t ts = static_cast<uint64_t>(now_ms);
    b[0] = static_cast<uint8_t>((ts >> 40) & 0xff);
    b[1] = static_cast<uint8_t>((ts >> 32) & 0xff);
    b[2] = static_cast<uint8_t>((ts >> 24) & 0xff);
    b[3] = static_cast<uint8_t>((ts >> 16) & 0xff);
    b[4] = static_cast<uint8_t>((ts >> 8) & 0xff);
    b[5] = static_cast<uint8_t>(ts & 0xff);

    uint64_t rand_a = dist(rng);
    uint64_t rand_b = dist(rng);
    b[6] = static_cast<uint8_t>((rand_a >> 56) & 0x0f) | 0x70;
    b[7] = static_cast<uint8_t>((rand_a >> 48) & 0xff);
    b[8] = static_cast<uint8_t>((rand_a >> 40) & 0x3f) | 0x80;
    b[9] = static_cast<uint8_t>((rand_a >> 32) & 0xff);
    b[10] = static_cast<uint8_t>((rand_a >> 24) & 0xff);
    b[11] = static_cast<uint8_t>((rand_a >> 16) & 0xff);
    b[12] = static_cast<uint8_t>((rand_a >> 8) & 0xff);
    b[13] = static_cast<uint8_t>(rand_a & 0xff);
    b[14] = static_cast<uint8_t>((rand_b >> 56) & 0xff);
    b[15] = static_cast<uint8_t>((rand_b >> 48) & 0xff);

    static const char hex[] = "0123456789abcdef";
    sstring out{sstring::initialized_later(), 36};
    auto *p = out.data();
    for (int i = 0; i < 4; ++i) {
      *p++ = hex[b[i] >> 4], *p++ = hex[b[i] & 0xf];
    }
    *p++ = '-';
    for (int i = 4; i < 6; ++i) {
      *p++ = hex[b[i] >> 4], *p++ = hex[b[i] & 0xf];
    }
    *p++ = '-';
    for (int i = 6; i < 8; ++i) {
      *p++ = hex[b[i] >> 4], *p++ = hex[b[i] & 0xf];
    }
    *p++ = '-';
    for (int i = 8; i < 10; ++i) {
      *p++ = hex[b[i] >> 4], *p++ = hex[b[i] & 0xf];
    }
    *p++ = '-';
    for (int i = 10; i < 16; ++i) {
      *p++ = hex[b[i] >> 4], *p++ = hex[b[i] & 0xf];
    }
    return out;
  }
};

seastar::sstring generate_random_logname() {
  seastar::sstring out{sstring::initialized_later(), 45};
  auto *p = out.data();
  *p++ = 's';
  *p++ = 'm';
  *p++ = 't';
  *p++ = 'p';
  *p++ = '-';
  seastar::sstring random_uuid = uuidv7::generate();
  for (char c : random_uuid) {
    if (c != '\n') {
      *p++ = c;
    }
  }
  *p++ = '.';
  *p++ = 'l';
  *p++ = 'o';
  *p++ = 'g';
  return out;
}

seastar::sstring generate_email_filename() {
  seastar::sstring out{sstring::initialized_later(), 40};
  auto *p = out.data();
  seastar::sstring random_uuid = uuidv7::generate();
  for (char c : random_uuid) {
    if (c != '\n') {
      *p++ = c;
    }
  }
  *p++ = '.';
  *p++ = 'e';
  *p++ = 'm';
  *p++ = 'l';
  return out;
}

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

constexpr bool has_smtp_command(const char *a, const std::string &b) {
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

  return i == b.size();
}

seastar::future<> handle_connection(seastar::connected_socket cs,
                                    seastar::socket_address remote,
                                    uint32_t timeout_seconds,
                                    seastar::gate &gate,
                                    seastar::abort_source &as) {
  auto [ip, ec] = get_ip_address(remote);
  applog.info("New client {} connection", ip);
  seastar::sstring log_filename = "/tmp/" + generate_random_logname();
  auto tmp_file = co_await seastar::open_file_dma(
      log_filename, seastar::open_flags::rw | seastar::open_flags::create);
  applog.info("Writing to {} for client {}", log_filename, remote);
  auto in = cs.input();
  auto out = cs.output();

  seastar::timer<> idle_timer;
  size_t bytes_consumed = 0;
  bool active = true;

  auto sub = as.subscribe([&]() noexcept {
    active = false;
    idle_timer.cancel();
    applog.warn("aborting client {} ...", remote);
    try {
      cs.shutdown_input();
      cs.shutdown_output();
    } catch (...) {
      // void
    }
  });

  idle_timer.set_callback([&, remote] {
    active = false;
    applog.warn("client {} timeout, closing ...", remote);
    try {
      cs.shutdown_input();
      cs.shutdown_output();
    } catch (...) {
      // void
    }
  });
  idle_timer.arm(std::chrono::seconds(timeout_seconds));

  gate.enter();

  co_await out.write("220 mail Service ready\r\n");
  co_await out.flush();

  std::vector<seastar::temporary_buffer<char>> chunks;
  size_t offset = 0;
  bool lineErr = false;
  bool inDataMode = false;

  try {
    while (active) {
      auto buf = co_await in.read();
      if (buf.empty()) {
        break;
      }

      lineErr = false;
      idle_timer.rearm(seastar::timer<>::clock::now() +
                       std::chrono::seconds(timeout_seconds));

      chunks.push_back(buf.share());
      co_await tmp_file.dma_write(offset, buf.get(), buf.size());
      offset += buf.size();

      if (inDataMode) {
        // TODO: write maildir data
        auto [found, consumed] = find_data_delimeter(chunks);
        if (found) {
          inDataMode = false;
          chunks.clear();
          co_await out.write("250 OK: message queued\r\n");
          co_await out.flush();
        }
      } else {
        auto [prefix, found, consumed] = find_first_crlf(chunks);
        if (found) {
          chunks.clear();
          if (prefix.find("DATA") != std::string_view::npos) {
            inDataMode = true;
            co_await out.write(
                "354 Start mail input; end with <CRLF>.<CRLF>\r\n");
          } else if ((prefix.find("EHLO ") != std::string_view::npos) ||
                     (prefix.find("HELO ") != std::string_view::npos)) {

            co_await out.write("250-maildomain.ngo Nice to meet you, [");
            auto [ip, ec] = get_ip_address(remote);
            co_await out.write(ip, std::strlen(ip));
            co_await out.write("]\r\n");
            co_await out.write("250-8BITMIME\r\n");
            co_await out.write("250-SMTPUTF8\r\n");
            // Per the SMTP RFC standards: 512 KB × 1024 bytes/KB = 524,288
            // bytes
            co_await out.write("250 SIZE 524288\r\n");
          } else if (prefix.find("QUIT") != std::string_view::npos) {
            co_await out.write("221 Bye\r\n");
          } else {
            if (lineErr) {
              co_await out.write("500 Syntax error\r\n");
            } else {
              co_await out.write("250 OK\r\n");
            }
          }
          co_await out.flush();
        }
      }
    }
    applog.info("Finished writing {} bytes to {} for client {}", offset,
                log_filename, remote);
  } catch (const seastar::timed_out_error &e) {
    applog.info("Client {} idle timeout", remote);
  } catch (const std::exception &ex) {
    applog.warn("Connection {} error: {}", remote, ex.what());
  }

  idle_timer.cancel();

  try {
    co_await in.close();
  } catch (...) {
  }
  try {
    co_await out.close();
  } catch (...) {
  }

  try {
    co_await tmp_file.close();
  } catch (const std::exception &ex) {
    applog.warn("Error closing file for {}: {}", remote, ex.what());
  }
  applog.info("Client {} connection finished", remote);

  gate.leave();

  co_return;
}

seastar::future<> serve(uint16_t port, seastar::abort_source &as,
                        seastar::gate &gate, uint32_t timeout_seconds) {
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
                              gate, as)
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
  std::cout << PROJECT_VERSION << std::endl;
  seastar::app_template app;
  namespace po = boost::program_options;
  app.add_options()("port,p", po::value<uint16_t>()->default_value(5255),
                    "SMTP port to listen on")(
      "timeout,t", po::value<uint32_t>()->default_value(30),
      "Client idle timeout in seconds");
  return app.run(argc, argv, [&app]() -> seastar::future<> {
    uint16_t port = app.configuration()["port"].as<uint16_t>();
    uint32_t timeout_seconds = app.configuration()["timeout"].as<uint32_t>();

    auto stop_signal = std::make_shared<seastar_apps_lib::stop_signal>();

    seastar::sharded<seastar::gate> gate;
    co_await gate.start();

    seastar::sharded<seastar::abort_source> abort_sources;
    co_await abort_sources.start();

    auto shards_future = seastar::smp::invoke_on_all([port, &abort_sources,
                                                      &gate, timeout_seconds] {
      return serve(port, abort_sources.local(), gate.local(), timeout_seconds);
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