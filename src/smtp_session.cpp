#include "smtp_session.hpp"

smtp_session::smtp_session(connected_socket cs_obj) : cs(std::move(cs_obj)) {
  in = std::make_unique<seastar::input_stream<char>>(cs.input());
  out = std::make_unique<seastar::output_stream<char>>(cs.output());
  plain_in = {};
  plain_out = {};
}

future<> smtp_session::init_logfile(const sstring &filename) {
  if (logfile.has_value()) {
    co_return;
  }

  seastar::file file_log =
      co_await open_file_dma(filename, open_flags::rw | open_flags::create);
  logfile = dma_file_writer(std::move(file_log));
}

future<> smtp_session::init_emailfile(const sstring &filename) {
  if (emailfile.has_value()) {
    co_return;
  }

  seastar::file_output_stream_options opts;
  opts.preallocation_size = DISABLE_BUFFER_PREALLOCATION_SIZE;
  opts.write_behind = 1;
  seastar::file file_email =
      co_await open_file_dma(filename, open_flags::rw | open_flags::create);
  opts.buffer_size = std::max<size_t>(file_email.disk_write_dma_alignment(),
                                      DEFAULT_BUFFER_ALIGNMENT_SIZE);
  emailfile =
      co_await seastar::make_file_output_stream(std::move(file_email), opts);
}

future<temporary_buffer<char>> smtp_session::read_input() {
  co_return co_await in->read();
}

future<temporary_buffer<char>>
smtp_session::read_input_exactly(const size_t &length) {
  co_return co_await in->read_exactly(length);
}

future<> smtp_session::upgrade_tls(shared_ptr<tls::server_credentials> certs) {
  /*** IMPORTANT: Workarounds to cleanup and prevent socket leaks ***/
  co_await out->flush();

  plain_in = std::move(in);
  plain_out = std::move(out);

  (void)out.release();
  co_await seastar::yield();

  (void)in.release();
  cs = co_await tls::wrap_server(certs, std::move(cs));

  in = std::make_unique<seastar::input_stream<char>>(cs.input());
  out = std::make_unique<seastar::output_stream<char>>(cs.output());

  is_tls = true;

  co_return;
}

future<> smtp_session::write_data(temporary_buffer<char> data) {
  state_emailfile_pos += data.size();
  co_await emailfile->write(data.get(), data.size());
}

future<> smtp_session::write_data(sstring data) {
  state_emailfile_pos += data.size();
  co_await emailfile->write(data.data(), data.size());
}

future<> smtp_session::write_log(temporary_buffer<char> data) {
  state_logfile_pos += data.size();
  co_await logfile->write(data.get(), data.size());
}

future<> smtp_session::send(sstring message) {
  if (message.empty()) {
    co_return;
  }

  state_logfile_pos += message.size();

  try {
    co_await logfile->write(message.data(), message.size());
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

const uint64_t &smtp_session::get_email_size() const {
  return state_emailfile_pos;
}

const uint64_t &smtp_session::get_log_size() const { return state_logfile_pos; }

future<> smtp_session::close() {
  co_await out->close();
  co_await in->close();
  cs.shutdown_output();
  cs.shutdown_input();

  if (emailfile) {
    co_await emailfile->flush();
    co_await emailfile->close();
    emailfile.reset();
  }
  if (logfile) {
    co_await logfile->close();
    logfile.reset();
  }

  out.reset();
  in.reset();

  if (plain_out) {
    co_await plain_out->close();
    plain_out.reset();
  }

  if (plain_in) {
    co_await plain_in->close();
    plain_in.reset();
  }
}

smtp_session::~smtp_session() {
  // TODO: conditional debug on compile time
  applog.trace("smtp_session destructor...");
}