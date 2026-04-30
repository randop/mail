#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <seastar/core/file.hh>
#include <seastar/core/future.hh>
#include <vector>

class dma_file_writer {
public:
  explicit dma_file_writer(seastar::file f);
  ~dma_file_writer();

  dma_file_writer(const dma_file_writer &) = delete;
  dma_file_writer &operator=(const dma_file_writer &) = delete;

  dma_file_writer(dma_file_writer &&o) noexcept;
  dma_file_writer &operator=(dma_file_writer &&o) noexcept;

  seastar::future<> write(const char *data, size_t size);
  seastar::future<> close();

private:
  struct segment {
    const char *data;
    size_t size;
    size_t logical_size;
  };

  seastar::file _file;

  size_t _pos = 0;
  size_t _align = 0;

  char *_buf = nullptr;
  size_t _buf_used = 0;

  static constexpr size_t batch_limit = 64;
  std::vector<segment> _batch;

private:
  seastar::future<> flush_batch();
  seastar::future<> drain_batch(bool final);

  seastar::future<> write_physical(const char *data, size_t size);

  void cleanup() noexcept;
};