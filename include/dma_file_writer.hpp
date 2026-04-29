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

  seastar::future<> write(const char *data, size_t size);

  seastar::future<> close();

private:
  seastar::file _file;
  uint64_t _pos{0};
  size_t _align{4096};

  seastar::temporary_buffer<char> _buffer;
  size_t _buf_used{0};

  seastar::future<> flush_aligned();
  seastar::future<> write_all(const char *data, size_t size);
};