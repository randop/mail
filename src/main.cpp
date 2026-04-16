/*****************************************************************************
mail
  Privacy-first and self-hosted email server for modern-era 2026 instead of
legacy 1999

Requires: Seastar v25.05.0 or latest

Copyright © 2010 — 2026 Randolph Ledesma

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

*****************************************************************************/

#include <chrono>
#include <iostream>
#include <seastar/core/app-template.hh>
#include <seastar/core/do_with.hh>
#include <seastar/core/future-util.hh>
#include <seastar/core/gate.hh>
#include <seastar/core/loop.hh>
#include <seastar/core/memory.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/sharded.hh>
#include <seastar/core/sleep.hh>
#include <seastar/core/thread.hh>
#include <seastar/net/api.hh>
#include <seastar/net/dns.hh>
#include <seastar/net/tls.hh>
#include <seastar/util/closeable.hh>
#include <seastar/util/log.hh>
#include <thread>

#include "stop_signal.hh"

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

static seastar::logger applog("smtp-server");

using namespace seastar;
using namespace std::chrono_literals;

namespace bpo = boost::program_options;

struct streams {
  connected_socket s;
  input_stream<char> in;
  output_stream<char> out;

  streams(connected_socket cs)
      : s(std::move(cs)), in(s.input()), out(s.output()) {}
};

class smtpserver {
  server_socket _socket;
  seastar::gate _gate;
  bool _stopped = false;
  bool _verbose = false;
  std::chrono::seconds _idle_timeout{300}; // default 5 minutes

  // IMPORTANT: Store the listening future per shard
  future<> _listen_future = make_ready_future<>();

public:
  smtpserver(bool verbose = false, std::chrono::seconds idle_timeout = 300s)
      : _verbose(verbose), _idle_timeout(idle_timeout) {}

  future<> listen(socket_address addr) {
    listen_options opts;
    opts.reuse_address = true;

    _socket = seastar::listen(seastar::make_ipv4_address(addr), opts);
    applog.info("[shard {}] Listening on {}", this_shard_id(), addr);

    // Background accept loop
    _listen_future = repeat([this] {
      if (_stopped) {
        return make_ready_future<stop_iteration>(stop_iteration::yes);
      }

      return with_gate(_gate, [this] {
        return _socket.accept()
            .then([this](accept_result ar) {
              connected_socket s = std::move(ar.connection);
              socket_address remote = std::move(ar.remote_address);

              if (_verbose) {
                applog.info("New connection from client {}", remote);
              }

              auto strms = make_lw_shared<streams>(std::move(s));

              // Per-connection idle timer
              // Per-connection idle timer
              auto idle_timer = make_lw_shared<timer<>>();

              // Safe timer callback - closes input stream on timeout

              idle_timer->set_callback([strms, remote, this] {
                if (_verbose) {
                  applog.info("Idle timeout reached for {}", remote);
                }
                (void)strms->in.close(); // This will terminate the read loop
              });

              // Helper to safely arm the timer
              auto arm_idle_timer = [idle_timer, this] {
                if (idle_timer->armed()) {
                  idle_timer->cancel();
                }
                idle_timer->arm(_idle_timeout);
              };

              // Start the timer
              arm_idle_timer();

              return repeat([strms, idle_timer, arm_idle_timer, this,
                             remote]() mutable {
                       return strms->in.read().then(
                           [this, strms, arm_idle_timer,
                            idle_timer](temporary_buffer<char> buf) {
                             // Reset idle timer on every successful read
                             arm_idle_timer();

                             if (buf.empty()) {
                               if (_verbose) {
                                 std::cout << "EOM" << std::endl;
                               }
                               return make_ready_future<stop_iteration>(
                                   stop_iteration::yes);
                             }

                             sstring tmp(buf.begin(), buf.end());
                             if (_verbose) {
                               std::cout << "Read " << tmp.size() << "B"
                                         << std::endl;
                             }

                             return make_ready_future<stop_iteration>(
                                 stop_iteration::no);
                           });
                     })
                  .then([strms, idle_timer] {
                    idle_timer->cancel();
                    (void)strms->out.flush();
                    return strms->out.close();
                  })
                  .handle_exception([](std::exception_ptr) {
                    // Ignore per-connection errors (removed unused 'ep')
                  })
                  .finally([this, strms, idle_timer] {
                    if (_verbose) {
                      std::cout << "Ending session" << std::endl;
                    }
                    idle_timer->cancel();
                    return when_all_succeed(strms->in.close())
                        .discard_result()
                        .handle_exception([](std::exception_ptr) {});
                  });
            })
            .handle_exception([this](auto ep) {
              if (!_stopped) {
                applog.error("Accept error: {}", ep);
              }
            })
            .then([this] {
              return make_ready_future<stop_iteration>(
                  _stopped ? stop_iteration::yes : stop_iteration::no);
            });
      });
    });

    return make_ready_future<>();
  }

  future<> stop() {
    _stopped = true;
    _socket.abort_accept();
    return _gate.close().then([this] {
      return std::move(_listen_future); // Wait for accept loop to stop
    });
  }
};

int main(int ac, char **av) {
  bool version = false;
  if (version) {
    std::cout << PROJECT_VERSION << std::endl;
    exit(EXIT_SUCCESS);
  }

  std::cout << "version: " << PROJECT_VERSION << std::endl;

  app_template app;
  app.add_options()("port", bpo::value<uint16_t>()->default_value(10000),
                    "Server port")(
      "address", bpo::value<std::string>()->default_value("0.0.0.0"),
      "Server address")(
      "version,v",
      bpo::value<bool>()->default_value(false)->implicit_value(true),
      "Show version information and quit")(
      "cert", bpo::value<std::string>()->default_value(""),
      "Certificate file (PEM)")("key",
                                bpo::value<std::string>()->default_value(""),
                                "Private key file (PEM)");

  return app.run(ac, av, [&app] {
    return async([&app] {
      seastar_apps_lib::stop_signal stop_signal;
      auto &&config = app.configuration();

      uint16_t port = config["port"].as<uint16_t>();
      std::string addr_str = config["address"].as<std::string>();
      bool verbose = true;
      sstring cert_file = config["cert"].as<std::string>();
      sstring key_file = config["key"].as<std::string>();

      // === TLS Credentials Setup ===
      auto certs =
          make_shared<tls::server_credentials>(make_shared<tls::dh_params>());

      // Load server certificate + key
      bool willUsePEM = false;
      if (willUsePEM) {
        certs->set_x509_key_file(cert_file, key_file, tls::x509_crt_format::PEM)
            .get();
      }

      // Use system CA certificates (for client cert validation if enabled)
      certs->set_system_trust().get();

      // Optional: Set client auth to NONE (change to REQUIRE if you want client
      // certificates)
      certs->set_client_auth(tls::client_auth::NONE);

      net::inet_address a = net::dns::resolve_name(addr_str).get();
      ipv4_addr ia(a, port);
      socket_address sa(ia);

      std::chrono::seconds idle_timeout{30};

      seastar::sharded<smtpserver> server;
      server.start(verbose, idle_timeout).get();

      applog.info("shards: {}", seastar::smp::count);

      try {
        server.invoke_on_all(&smtpserver::listen, sa).get();
      } catch (...) {
        std::cerr << "Failed to start server: " << std::current_exception()
                  << std::endl;
        return EXIT_FAILURE;
      }

      applog.info("TCP server running at {}:{}", addr_str, port);
      applog.info("Press Ctrl+C to halt...");

      auto stats = seastar::memory::stats();

      std::cout << "Seastar memory stats (shard " << seastar::this_shard_id()
                << "):\n"
                << "  allocated:     " << stats.allocated_memory() / 1024 / 1024
                << " MiB\n"
                << "  free:          " << stats.free_memory() / 1024 / 1024
                << " MiB\n"
                << "  total:         "
                << (stats.allocated_memory() + stats.free_memory()) / 1024 /
                       1024
                << " MiB\n"
                << "  mallocs:       " << stats.mallocs() << "\n"
                << "  frees:         " << stats.frees() << "\n"
                << "  live objects:  " << stats.live_objects() << "\n"
                << std::endl;

      stop_signal.wait().get();

      applog.warn("Shutting down service ...");
      server.stop().get();
      return EXIT_SUCCESS;
    });
  });
}