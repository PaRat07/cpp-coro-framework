#pragma once

#include <chrono>
#include <coroutine>
#include <queue>
#include <algorithm>
#include <span>
#include <ranges>

#include "unistd.h"
#include "liburing.h"

#include "task.h"


struct File {
public:
  enum class CreateTag {
    kOpen,
    kCreate,
    kSocket,
    kAccept,
    kPipe,
    kFileno,
    kDup,
    kDup2

  };

private:
  int fd;
};

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
    size_t cnt;
    std::coroutine_handle<> handle;
  };

  void Resume(auto &&st_mach) {
      static constexpr size_t kMaxPeek = 20;
      std::array<io_uring_cqe*, kMaxPeek> cqes;
      size_t ready_cnt = io_uring_peek_batch_cqe(&holder.ring, cqes.data(), kMaxPeek);
      holder.cur_in -= ready_cnt;
      for (io_uring_cqe *copl : cqes | std::views::take(ready_cnt)) {
        std::coroutine_handle<>::from_address(std::bit_cast<void*>(copl->user_data)).resume();
        io_uring_cqe_seen(&holder.ring, copl);
      }
  }

  static bool Empty() {
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
