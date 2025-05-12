#pragma once

#include <chrono>
#include <coroutine>
#include <queue>
#include <algorithm>
#include <span>
#include <ranges>

#include "unistd.h"
#include "liburing.h"
#include <netinet/in.h>
#include <sys/socket.h>

#include "task.h"



struct UringHolder {
  UringHolder() {
    if (io_uring_queue_init(20, &ring, 0) < 0) {
      throw std::runtime_error("io_uring_queue_init failed");
    }
  }

  ~UringHolder() {
    io_uring_queue_exit(&ring);
  }

  io_uring ring;
  size_t cur_in = 0;
};

class IOUringEventLoop {
 public:
  struct ResHolder {
    int cnt = -132;
    std::coroutine_handle<> handle;
  };

  static void Resume() {
      static constexpr size_t kMaxPeek = 20;
      std::array<io_uring_cqe*, kMaxPeek> cqes;
      size_t ready_cnt = io_uring_peek_batch_cqe(&holder.ring, cqes.data(), kMaxPeek);
      holder.cur_in -= ready_cnt;
      for (io_uring_cqe *copl : cqes | std::views::take(ready_cnt)) {
        ResHolder &res_ref = *std::bit_cast<ResHolder*>(copl->user_data);
        std::clog << copl->res << std::endl;
        res_ref.cnt = copl->res;
        res_ref.handle.resume();
        io_uring_cqe_seen(&holder.ring, copl);
      }
  }

  static bool IsEmpty() {
    return holder.cur_in == 0;
  }


  struct ReadAwaitable {
    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle) noexcept {
      res.handle = handle;
      io_uring_sqe* sqe = io_uring_get_sqe(&holder.ring);
      io_uring_prep_read(sqe, fd, data.data(), data.size(), off);
      sqe->user_data = std::bit_cast<__u64>(&res);
      io_uring_submit(&holder.ring);
      ++holder.cur_in;
    }

    int await_resume() const noexcept {
      return res.cnt;
    }

    int fd;
    std::span<char8_t> data;
    off_t off;
    ResHolder res;
  };

  struct WriteAwaitable {
    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle) noexcept {
      res.handle = handle;
      io_uring_sqe* sqe = io_uring_get_sqe(&holder.ring);
      io_uring_prep_write(sqe, fd, data.data(), data.size(), off);
      sqe->user_data = std::bit_cast<__u64>(&res);
      io_uring_submit(&holder.ring);
      ++holder.cur_in;
    }

    int await_resume() const noexcept {
      return res.cnt;
    }

    int fd;
    std::span<const char8_t> data;
    off_t off;
    ResHolder res;
  };

  struct AcceptIPV4Awaitable {
    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle) noexcept {
      res.handle = handle;
      io_uring_sqe* sqe = io_uring_get_sqe(&holder.ring);
      io_uring_prep_accept(sqe, fd, nullptr, nullptr, 0);
      sqe->user_data = std::bit_cast<__u64>(&res);
      io_uring_submit(&holder.ring);
      ++holder.cur_in;
    }

    int await_resume() const noexcept {
      return res.cnt;
    }

    int fd;
    ResHolder res;
  };

 private:
  static inline UringHolder holder;
};

inline auto Read(int fd, std::span<char8_t> data, off_t off) -> Task<int> {
  co_return co_await IOUringEventLoop::ReadAwaitable {
    .fd = fd,
    .data = data,
    .off = off
  };
}

inline auto Write(int fd, std::span<const char8_t> data, off_t off) -> Task<int> {
  co_return co_await IOUringEventLoop::WriteAwaitable {
    .fd = fd,
    .data = data,
    .off = off
  };
}

consteval in_addr operator""_addr(const char *data, size_t sz) {
  std::string_view sv = { data, sz };
  uint32_t ans = 0;
  uint32_t cur_num = 0;
  for (char i : sv) {
    if (i == '.') {
      ans = ans * 256 + std::exchange(cur_num, 0);
    } else {
      cur_num = cur_num * 10 + (i - '0');
    }
  }
  ans = ans * 256 + cur_num;
  return { ans };
}

inline auto AcceptIPV4(int fd) -> Task<int> {
  co_return co_await IOUringEventLoop::AcceptIPV4Awaitable {
    .fd = fd
  };
}

