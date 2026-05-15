#include "dma_file_writer.hpp"

dma_file_writer::dma_file_writer(seastar::file f) : _file(std::move(f)) {

  _align = std::max<size_t>(_file.disk_write_dma_alignment(), 4096);

  _buf = static_cast<char *>(std::aligned_alloc(_align, _align));
  if (!_buf) {
    throw std::bad_alloc();
  }
}

dma_file_writer::~dma_file_writer() { cleanup(); }

dma_file_writer::dma_file_writer(dma_file_writer &&o) noexcept
    : _file(std::move(o._file)), _pos(o._pos), _align(o._align), _buf(o._buf),
      _buf_used(o._buf_used), _batch(std::move(o._batch)) {

  o._buf = nullptr;
  o._buf_used = 0;
  o._pos = 0;
}

dma_file_writer &dma_file_writer::operator=(dma_file_writer &&o) noexcept {
  if (this != &o) {
    cleanup();

    _file = std::move(o._file);
    _pos = o._pos;
    _align = o._align;
    _buf = o._buf;
    _buf_used = o._buf_used;
    _batch = std::move(o._batch);

    o._buf = nullptr;
    o._buf_used = 0;
    o._pos = 0;
  }
  return *this;
}

seastar::future<> dma_file_writer::write(const char *data, size_t size) {
  size_t offset = 0;

  while (offset < size) {
    size_t space = _align - _buf_used;
    size_t to_copy = std::min(space, size - offset);

    std::memcpy(_buf + _buf_used, data + offset, to_copy);

    _buf_used += to_copy;
    offset += to_copy;

    if (_buf_used == _align) {
      co_await flush_batch();
    }
  }
}

seastar::future<> dma_file_writer::close() {
  if (_buf_used) {
    std::memset(_buf + _buf_used, 0, _align - _buf_used);

    _batch.push_back({_buf, _align, _buf_used});

    _buf_used = 0;
  }

  co_await drain_batch(true);
}

seastar::future<> dma_file_writer::flush_batch() {
  if (_buf_used) {
    _batch.push_back({_buf, _buf_used, _buf_used});

    _buf_used = 0;
  }

  co_await drain_batch(false);
}

seastar::future<> dma_file_writer::drain_batch(bool final) {
  if (_batch.empty()) {
    co_return;
  }

  size_t i = 0;

  while (i < _batch.size()) {
    size_t end = std::min(i + batch_limit, _batch.size());

    for (size_t j = i; j < end; ++j) {
      const auto &s = _batch[j];
      co_await write_physical(s.data, s.size);

      _pos += s.logical_size;
    }

    i = end;
  }

  _batch.clear();

  if (final) {
    co_await _file.truncate(_pos);
  }
}

seastar::future<> dma_file_writer::write_physical(const char *data,
                                                  size_t size) {
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

void dma_file_writer::cleanup() noexcept {
  if (_buf) {
    std::free(_buf);
    _buf = nullptr;
  }
}