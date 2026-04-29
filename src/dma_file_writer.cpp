#include "dma_file_writer.hpp"

dma_file_writer::dma_file_writer(seastar::file f) : _file(std::move(f)) {
  _align = _file.disk_write_dma_alignment();
  _buffer = seastar::temporary_buffer<char>(_align);
}

seastar::future<> dma_file_writer::write(const char *data, size_t size) {
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

seastar::future<> dma_file_writer::close() {
  if (_buf_used == 0) {
    co_return;
  }

  // pad remaining bytes
  memset(_buffer.get_write() + _buf_used, 0, _align - _buf_used);

  co_await write_all(_buffer.get(), _align);
  _buf_used = 0;
}

seastar::future<> dma_file_writer::flush_aligned() {
  co_await write_all(_buffer.get(), _align);
  _buf_used = 0;
}

seastar::future<> dma_file_writer::write_all(const char *data, size_t size) {
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