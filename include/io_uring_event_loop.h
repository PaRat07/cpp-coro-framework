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
#include <poll.h>

#include "task.h"

namespace chr = std::chrono;
using namespace std::chrono_literals;

struct UringHolder {
  UringHolder() {
    if (io_uring_queue_init(32024, &ring, 0) < 0) {
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
    int cnt;
    std::coroutine_handle<> handle;
  };

  static void Resume() {
      static constexpr size_t kMaxPeek = 256;
      std::array<io_uring_cqe*, kMaxPeek> cqes;
      size_t ready_cnt = io_uring_peek_batch_cqe(&holder->ring, cqes.data(), kMaxPeek);
      holder->cur_in -= ready_cnt;
      for (io_uring_cqe *copl : cqes | std::views::take(ready_cnt)) {
        ResHolder &res_ref = *std::bit_cast<ResHolder*>(copl->user_data);
        res_ref.cnt = copl->res;
        res_ref.handle.resume();
        io_uring_cqe_seen(&holder->ring, copl);
      }
      io_uring_submit(&holder->ring);
  }

  static bool IsEmpty() {
    return holder->cur_in == 0;
  }

  static void Init() {
    holder.emplace();
  }


  struct ReadAwaitable {
    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle) noexcept {
      res.handle = handle;
      io_uring_sqe* sqe = io_uring_get_sqe(&holder->ring);
      io_uring_prep_read(sqe, fd, data.data(), data.size(), off);
      sqe->user_data = std::bit_cast<__u64>(&res);
      ++holder->cur_in;
    }

    int await_resume() const noexcept {
      return res.cnt;
    }

    int fd;
    std::span<char> data;
    off_t off;
    ResHolder res;
  };


  struct RecieveAwaitable {
    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle) noexcept {
      res.handle = handle;
      io_uring_sqe* sqe = io_uring_get_sqe(&holder->ring);
      io_uring_prep_recv(sqe, fd, data.data(), data.size(), flags);
      sqe->user_data = std::bit_cast<__u64>(&res);
      // sqe->ioprio = IORING_RECVSEND_POLL_FIRST;
      ++holder->cur_in;
    }

    int await_resume() const noexcept {
      return res.cnt;
    }

    int fd;
    std::span<char> data;
    int flags;
    ResHolder res;
  };

  struct WriteAwaitable {
    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle) noexcept {
      res.handle = handle;
      io_uring_sqe* sqe = io_uring_get_sqe(&holder->ring);
      io_uring_prep_write(sqe, fd, data.data(), data.size(), off);
      sqe->user_data = std::bit_cast<__u64>(&res);
      ++holder->cur_in;
    }

    int await_resume() const noexcept {
      return res.cnt;
    }

    int fd;
    std::span<const char> data;
    off_t off;
    ResHolder res;
  };

  struct SendAwaitable {
    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle) noexcept {
      res.handle = handle;
      io_uring_sqe* sqe = io_uring_get_sqe(&holder->ring);
      io_uring_prep_send(sqe, fd, data.data(), data.size(), flags);
      sqe->user_data = std::bit_cast<__u64>(&res);
      sqe->ioprio = IORING_RECVSEND_POLL_FIRST;
      ++holder->cur_in;
    }

    int await_resume() const noexcept {
      return res.cnt;
    }

    int fd;
    std::span<const char> data;
    int flags;
    ResHolder res;
  };

  struct AcceptIPV4Awaitable {
    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle) noexcept {
      res.handle = handle;
      io_uring_sqe* sqe = io_uring_get_sqe(&holder->ring);
      io_uring_prep_accept(sqe, fd, nullptr, nullptr, SOCK_NONBLOCK);
      sqe->user_data = std::bit_cast<__u64>(&res);
      ++holder->cur_in;
    }

    int await_resume() const noexcept {
      return res.cnt;
    }

    int fd;
    ResHolder res;
  };

  struct PollAwaitable {
    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle) noexcept {
      res.handle = handle;
      io_uring_sqe* sqe = io_uring_get_sqe(&holder->ring);
      io_uring_prep_poll_add(sqe, fd, POLLIN);
      sqe->user_data = std::bit_cast<__u64>(&res);
      ++holder->cur_in;
    }

    int await_resume() const noexcept {
      return res.cnt;
    }

    int fd;
    ResHolder res;
  };

 private:
  static inline size_t cur_queued_until_submit_ = 0;
  static inline std::chrono::steady_clock::time_point time_first_not_submitted_;
  static inline std::optional<UringHolder> holder;
};

inline auto Read(int fd, std::span<char> data, off_t off) -> Task<size_t> {
  co_return co_await IOUringEventLoop::ReadAwaitable {
    .fd = fd,
    .data = data,
    .off = off
  };
}

inline auto Recieve(int fd, std::span<char> data, int flags = 0) -> Task<size_t> {
  co_return co_await IOUringEventLoop::RecieveAwaitable {
    .fd = fd,
    .data = data,
    .flags = flags
  };
}

inline auto Write(int fd, std::span<const char> data, off_t off) -> Task<size_t> {
  co_return co_await IOUringEventLoop::WriteAwaitable {
    .fd = fd,
    .data = data,
    .off = off
  };
}

inline auto Send(int fd, std::span<const char> data, int flags) -> Task<size_t> {
  co_return co_await IOUringEventLoop::SendAwaitable {
    .fd = fd,
    .data = data,
    .flags = flags
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


inline auto Poll(int fd) -> Task<int> {
  co_return co_await IOUringEventLoop::PollAwaitable {
    .fd = fd
  };
}

