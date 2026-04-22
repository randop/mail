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
#include <seastar/core/future-util.hh>
#include <seastar/core/future.hh>
#include <seastar/core/gate.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/sharded.hh>
#include <seastar/core/shared_ptr.hh>
#include <seastar/core/timer-set.hh>
#include <seastar/net/api.hh>
#include <seastar/util/closeable.hh>
#include <seastar/util/log.hh>
#include <seastar/util/tmp_file.hh>

#include "stop_signal.hh"

#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <random>
#include <utility>

static seastar::logger applog("smtp-server");

seastar::future<> handle_connection(seastar::connected_socket cs,
                                    seastar::socket_address remote,
                                    uint32_t timeout_seconds,
                                    seastar::gate &gate,
                                    seastar::abort_source &as) {
  applog.info("New client {} connection", remote);
  auto seed = std::chrono::system_clock::now().time_since_epoch().count();
  auto rnd = std::mt19937(seed);
  seastar::sstring client_filename =
      "/tmp/" + std::to_string(rnd()) + ".data.log";
  auto tmp_file = co_await seastar::open_file_dma(
      client_filename, seastar::open_flags::rw | seastar::open_flags::create);
  applog.info("Writing to {} for client {}", client_filename, remote);
  auto in = cs.input();
  auto out = cs.output();

  seastar::timer<> idle_timer;
  bool active = true;

  auto sub = as.subscribe([&]() noexcept {
    active = false;
    idle_timer.cancel();
    applog.warn("aborting client {} ...", remote);
    (void)in.close();
    (void)out.close();
  });

  idle_timer.set_callback([&, remote] {
    active = false;
    applog.warn("client {} timeout, closing ...", remote);
    (void)in.close();
    (void)out.close();
  });
  idle_timer.arm(std::chrono::seconds(timeout_seconds));

  gate.enter();

  try {
    size_t offset = 0;
    while (active) {
      auto buf = co_await in.read();
      if (buf.empty()) {
        break;
      }
      idle_timer.rearm(seastar::timer<>::clock::now() +
                       std::chrono::seconds(timeout_seconds));

      co_await tmp_file.dma_write(offset, buf.get(), buf.size());
      offset += buf.size();
    }
    applog.info("Finished writing {} bytes to {} for client {}", offset,
                client_filename, remote);
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
  timer.set_callback([&connection_count, &timer] {
    applog.info("Active connections on shard {}: {}", seastar::this_shard_id(),
                connection_count);
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
            } catch (const std::exception &err) {
              applog.error("Error closing connections: {}", err.what());
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
  app.add_options()("port,p", po::value<uint16_t>()->default_value(8080),
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