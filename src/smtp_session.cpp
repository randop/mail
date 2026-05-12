#include "smtp_session.hpp"

smtp_session::smtp_session(connected_socket cs_obj)
    : cs(std::move(cs_obj)), in(cs.input()), out(cs.output()) {}

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

  seastar::file file_email =
      co_await open_file_dma(filename, open_flags::rw | open_flags::create);
  emailfile = dma_file_writer(std::move(file_email));
}

future<temporary_buffer<char>> smtp_session::read_input() {
  co_return co_await in.read();
}
future<temporary_buffer<char>> smtp_session::read_input_exactly(size_t length) {
  co_return co_await in.read_exactly(length);
}

future<> smtp_session::upgrade_tls(shared_ptr<tls::server_credentials> certs) {
  co_await out.flush();

  input_stream<char> plain_in = std::move(in);
  output_stream<char> plain_out = std::move(out);

  cs = co_await tls::wrap_server(certs, std::move(cs));
  out = cs.output();
  in = cs.input();
  is_tls = true;
  try {
    // co_await plain_out.close();
    // co_await plain_in.close();
  } catch (...) {
    // void
  }
}

future<> smtp_session::write_data(temporary_buffer<char> data) {
  state_emailfile_pos += data.size();
  co_await emailfile->write(data.get(), data.size());
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

  co_await out.write(message);
  co_await out.flush();

  co_return;
}

uint64_t smtp_session::get_email_size() { return state_emailfile_pos; }

uint64_t smtp_session::get_log_size() { return state_logfile_pos; }

future<> smtp_session::close() {
  co_await out.flush();
  co_await out.close();
  cs.shutdown_output();
  co_await in.close();
  cs.shutdown_input();

  if (emailfile) {
    co_await emailfile->close();
  }
  if (logfile) {
    co_await logfile->close();
  }
}