#pragma once

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
#include <seastar/util/defer.hh>
#include <seastar/util/log.hh>
#include <seastar/util/tmp_file.hh>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <format>
#include <functional>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "dma_file_writer.hpp"
#include "file_helpers.hpp"
#include "logger.hpp"

using namespace seastar;

class smtp_session {
public:
  explicit smtp_session(connected_socket cs_obj);

  future<> init_logfile(const sstring &filename);

  future<> init_emailfile(const sstring &filename);

  future<temporary_buffer<char>> read_input();
  future<temporary_buffer<char>> read_input_exactly(const size_t &length);

  future<> write_data(temporary_buffer<char> data);
  future<> write_log(temporary_buffer<char> data);

  future<> upgrade_tls(shared_ptr<tls::server_credentials> certs);

  future<> send(sstring message);

  const uint64_t &get_email_size() const;
  const uint64_t &get_log_size() const;

  future<> close();

  ~smtp_session();

private:
  connected_socket cs;

  std::unique_ptr<seastar::input_stream<char>> in;
  std::unique_ptr<seastar::output_stream<char>> out;
  std::unique_ptr<seastar::input_stream<char>> plain_in;
  std::unique_ptr<seastar::output_stream<char>> plain_out;

  std::optional<seastar::output_stream<char>> emailfile;
  uint64_t state_emailfile_pos = 0;
  std::optional<dma_file_writer> logfile;
  uint64_t state_logfile_pos = 0;
  bool is_tls = false;
};