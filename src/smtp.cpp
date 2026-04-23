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

#include <cctype>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <random>
#include <seastar/core/app-template.hh>
#include <seastar/core/coroutine.hh>
#include <seastar/core/file.hh>
#include <seastar/core/fstream.hh>
#include <seastar/core/loop.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/seastar.hh>
#include <seastar/core/sharded.hh>
#include <seastar/core/sleep.hh>
#include <seastar/net/api.hh>
#include <seastar/net/tls.hh>
#include <seastar/util/log.hh>
#include <string>
#include <string_view>
#include <vector>

using namespace seastar;

logger applog("smtp");

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

seastar::sstring generate_random_name() {
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

seastar::sstring generate_email_name() {
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

struct smtp_session {
  connected_socket s;
  std::unique_ptr<output_stream<char>> out;
  input_stream<char> in;
  std::unique_ptr<output_stream<char>> fout;
  bool is_tls = false;

  smtp_session(connected_socket s, output_stream<char> fout_obj)
      : s(std::move(s)),
        out(std::make_unique<output_stream<char>>(this->s.output())),
        in(this->s.input()),
        fout(std::make_unique<output_stream<char>>(std::move(fout_obj))) {}

  future<> send(std::string_view msg) {
    if (fout) {
      co_await fout->write(msg.data(), msg.size());
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

    s = co_await tls::wrap_server(certs, std::move(s));
    out = std::make_unique<output_stream<char>>(s.output());
    in = s.input();
    is_tls = true;
  }

  future<> close() {
    s.shutdown_output();
    s.shutdown_input();
    if (out) {
      co_await out->close();
      out = nullptr;
    }
    if (fout) {
      co_await fout->close();
      fout = nullptr;
    }
  }
};

future<> handle_connection(connected_socket ssocket, socket_address remote,
                           shared_ptr<tls::server_credentials> certs) {
  auto filename = generate_random_name();
  seastar::sstring email_filename = generate_email_name();
  applog.info("Session {} created log file {}", remote, filename);
  std::unique_ptr<smtp_session> sess;

  seastar::sstring accum;
  std::string_view cmd_view;
  size_t cmd_pos = 0;
  size_t cmd_start_index = 0;
  size_t crlf_pos = 0;
  bool in_command = true;
  bool in_crlf = false;
  bool in_cmd_boundary = false;

  seastar::sstring data_buffer;
  bool in_data = false;
  size_t data_pos = 0;
  size_t data_size = 0;

  bool active = true;
  bool is_data_ended = false;

  uint64_t email_pos = 0;

  try {
    seastar::file logfile = co_await open_file_dma(
        filename, open_flags::rw | open_flags::create | open_flags::truncate);

    seastar::file emailfile = co_await seastar::open_file_dma(
        email_filename, seastar::open_flags::rw | seastar::open_flags::create);

    uint64_t emailfile_align = emailfile.disk_write_dma_alignment();
    std::string email_buffer;
    email_buffer.reserve(emailfile_align * 2);

    sess = std::make_unique<smtp_session>(
        std::move(ssocket),
        co_await make_file_output_stream(std::move(logfile)));

    co_await sess->send("220 maildomain.ngo ESMTP ready\r\n");

    while (active) {
      temporary_buffer<char> buf = co_await sess->in.read();
      if (buf.empty()) {
        break;
      }

      if (!in_data) {
        accum.append(buf.get(), buf.size());
        if (sess->fout) {
          co_await sess->fout->write(buf.get(), buf.size());
        }
      }

      if (in_data) {
        applog.info("IN DATA MODE...");
        data_size += buf.size();
        // LIMIT: SIZE 524288
        if (data_size > 524288) {
          co_await sess->send("552 5.3.4 Message size limit exceeded\r\n");
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
            co_await sess->send("250 OK: message queued\r\n");
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
              co_await sess->send("250 OK: message queued\r\n");
            }
          }
        }

        email_buffer.append(buf.get(), buf.size());
        if (email_buffer.size() >= emailfile_align) {
          applog.info("DMA_WRITE: email content");
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
          if ((cmd_pos + 4) <= accum.size()) {
            auto *p = accum.data() + cmd_pos;
            if (has_smtp_command(p, "EHLO") || has_smtp_command(p, "HELO")) {
              applog.info("EHLO / HELO found at {}", cmd_pos);
              cmd_start_index = cmd_pos;
              in_cmd_boundary = true;
              cmd_pos += 4;
            } else if (has_smtp_command(p, "QUIT")) {
              applog.info("QUIT found at {}", cmd_pos);
              cmd_start_index = cmd_pos;
              in_cmd_boundary = true;
              cmd_pos += 4;
            } else if (has_smtp_command(p, "AUTH")) {
              applog.info("AUTH found at {}", cmd_pos);
              cmd_start_index = cmd_pos;
              in_cmd_boundary = true;
              cmd_pos += 4;
            } else if (has_smtp_command(p, "DATA")) {
              applog.info("DATA found at {}", cmd_pos);
              cmd_start_index = cmd_pos;
              in_command = false;
              in_data = true;
              in_cmd_boundary = true;
              cmd_pos += 4;
            }
          }
          if ((cmd_pos + 8) <= accum.size()) {
            auto *p = accum.data() + cmd_pos;
            if (has_smtp_command(p, "RCPT TO:")) {
              applog.info("RCPT TO found at {}", cmd_pos);
              cmd_start_index = cmd_pos;
              in_cmd_boundary = true;
              cmd_pos += 8;
            }
            if (has_smtp_command(p, "STARTTLS")) {
              applog.info("STARTTLS found at {}", cmd_pos);
              cmd_start_index = cmd_pos;
              in_cmd_boundary = true;
              cmd_pos += 8;
            }
          }
          if ((cmd_pos + 10) <= accum.size()) {
            auto *p = accum.data() + cmd_pos;
            if (has_smtp_command(p, "MAIL FROM:")) {
              applog.info("MAIL FROM found at {}", cmd_pos);
              cmd_start_index = cmd_pos;
              in_cmd_boundary = true;
              cmd_pos += 10;
            }
          }
        }

        crlf_pos = cmd_pos;

        if (in_cmd_boundary) {
          while (crlf_pos + 1 < accum.size()) {
            if (accum[crlf_pos] == '\r' && accum[crlf_pos + 1] == '\n') {
              applog.info("<CRLF> found at {}", crlf_pos);
              crlf_pos += 2;
              in_command = true;
              in_crlf = true;

              size_t p_size = (crlf_pos - cmd_start_index) - 1;
              if (p_size < 0) {
                p_size = 0;
              }
              cmd_view =
                  std::string_view(accum.data() + cmd_start_index, p_size);

              break;
            }
            ++crlf_pos;
          }
        } else {
          // clear
          cmd_view = std::string_view(accum.data(), 0);
          applog.info("clear cmd_view: {}", cmd_view);
        }

        if (in_command && in_crlf) {
          cmd_pos = crlf_pos;
          cmd_start_index = cmd_pos;
          applog.info("command: {}", cmd_view);

          if (cmd_view.starts_with("EHLO ") || cmd_view.starts_with("HELO ")) {

            co_await sess->out->write("250-maildomain.ngo Nice to meet you, [");
            co_await sess->fout->write(
                "250-maildomain.ngo Nice to meet you, [");
            char ipbuf[INET6_ADDRSTRLEN];
            const char *res = nullptr;

            const sockaddr &sa = remote.as_posix_sockaddr();

            if (sa.sa_family == AF_INET) {
              res = inet_ntop(
                  AF_INET, &reinterpret_cast<const sockaddr_in &>(sa).sin_addr,
                  ipbuf, sizeof(ipbuf));
            } else {
              res = inet_ntop(
                  AF_INET6,
                  &reinterpret_cast<const sockaddr_in6 &>(sa).sin6_addr, ipbuf,
                  sizeof(ipbuf));
            }

            co_await sess->out->write(ipbuf, std::strlen(ipbuf));
            co_await sess->fout->write(ipbuf, std::strlen(ipbuf));
            co_await sess->out->write("]\r\n");
            co_await sess->fout->write("]\r\n");
            co_await sess->out->write("250-8BITMIME\r\n");
            co_await sess->fout->write("250-8BITMIME\r\n");
            co_await sess->out->write("250-SMTPUTF8\r\n");
            co_await sess->fout->write("250-SMTPUTF8\r\n");
            co_await sess->out->write("250-STARTTLS\r\n");
            co_await sess->fout->write("250-STARTTLS\r\n");
            // Per the SMTP RFC standards: 512 KB × 1024 bytes/KB = 524,288
            // bytes
            co_await sess->out->write("250 SIZE 524288\r\n");
            co_await sess->fout->write("250 SIZE 524288\r\n");
            co_await sess->out->flush();
          } else if (cmd_view.starts_with("RCPT TO:")) {
            co_await sess->send("250 Accepted\r\n");
          } else if (cmd_view.starts_with("MAIL FROM:")) {
            co_await sess->send("250 Accepted\r\n");
          } else if (cmd_view.starts_with("STARTTLS")) {
            co_await sess->send("220 Ready to start TLS\r\n");
            co_await sess->upgrade_tls(certs);
            applog.info("Session {} upgraded to TLS", remote);
          } else if (cmd_view.starts_with("DATA")) {
            in_data = true;
            in_command = false;
            co_await sess->send(
                "354 Start mail input; end with <CR><LF>.<CR><LF>\r\n");
          } else if (cmd_view.starts_with("AUTH")) {
            co_await sess->send("502 Command not implemented\r\n");
          } else if (cmd_view.starts_with("QUIT")) {
            applog.info("got smtp quit");
            co_await sess->send("221 Bye\r\n");
          } else {
            co_await sess->send("500 Syntax error\r\n");
          }

          in_crlf = false;

          // clear
          cmd_view = std::string_view(accum.data(), 0);
        }
      }
    }

    // final tail (pad once)
    if (!email_buffer.empty()) {
      applog.info("DMA_WRITE: email remnant");
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

  } catch (const std::exception &ex) {
    applog.error("handle_connection error: {}", ex.what());
  }

  co_await sess->close();

  applog.info("Email file written: {}", email_filename);

  co_return;
}

future<> service_loop(uint16_t port) {
  auto certs = make_shared<tls::server_credentials>();
  co_await certs->set_x509_key_file("cert.pem", "key.pem",
                                    tls::x509_crt_format::PEM);

  listen_options lo;
  lo.reuse_address = true;
  server_socket listener = listen(make_ipv4_address({port}), lo);
  applog.info("SMTP server listening on port {} ...", port);

  while (true) {
    try {
      accept_result res = co_await listener.accept();
      (void)handle_connection(std::move(res.connection),
                              std::move(res.remote_address), certs);
    } catch (const std::exception &e) {
      applog.error("Accept error: {}", e.what());
    }
  }
}

int main(int argc, char **argv) {
  app_template app;
  uint16_t port = 2525;
  return app.run(argc, argv, [&port]() -> future<int> {
    co_await service_loop(port);
    co_return 0;
  });
}
