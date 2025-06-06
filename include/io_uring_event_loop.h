#pragma once

#include <algorithm>
#include <chrono>
#include <coroutine>
#include <queue>
#include <ranges>
#include <span>

#include "liburing.h"
#include "unistd.h"
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>

#include "sys_utility.h"
#include "task.h"
#include "coro_utility.h"
namespace uring {
namespace chr = std::chrono;
using namespace std::chrono_literals;

struct UringHolder {
  void Init() {
    Unwrap(io_uring_queue_init(4000, &ring, 0));
  }

  ~UringHolder() { io_uring_queue_exit(&ring); }

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
    io_uring_cqe *cqe;
    unsigned head;
    size_t cur_proc = 0;
    io_uring_for_each_cqe(&holder.ring, head, cqe) {
      ResHolder &res_ref = *std::bit_cast<ResHolder*>(cqe->user_data);
      res_ref.cnt = std::max(0, cqe->res);
      --holder.cur_in;
      res_ref.handle.resume();
      ++cur_proc;
    }
    io_uring_cq_advance(&holder.ring, cur_proc);
    if (not_submitted_cnt_ > 0) [[likely]] {
      Unwrap(io_uring_submit(&holder.ring));
      not_submitted_cnt_ = 0;
    }
  }

  static bool IsEmpty() { return holder.cur_in == 0; }

  static void Init() { holder.Init(); }

  friend struct File;

  struct PollAwaitable {
    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle) noexcept {
      res.handle = handle;
      io_uring_sqe *sqe = io_uring_get_sqe(&holder.ring);
      sqe->user_data = std::bit_cast<__u64>(&res);
      io_uring_prep_poll_add(sqe, fd, POLLIN);
      ++holder.cur_in;
      ++not_submitted_cnt_;
    }

    int await_resume() const noexcept { return res.cnt; }

    int fd;
    ResHolder res;
  };

private:
  static inline size_t not_submitted_cnt_ = 0;
  static inline UringHolder holder;
};

consteval in_addr operator""_addr(const char *data, size_t sz) {
  std::string_view sv = {data, sz};
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
  return {ans};
}

struct File {
public:
  auto Read(std::span<char> data, off_t off) -> Task<size_t> {
    struct ReadAwaitable {
      bool await_ready() const noexcept { return false; }

      void await_suspend(std::coroutine_handle<> handle) noexcept {
        res.handle = handle;
        io_uring_sqe *sqe = io_uring_get_sqe(&IOUringEventLoop::holder.ring);
        if (sqe == nullptr) [[unlikely]] {
          IOUringEventLoop::not_submitted_cnt_ = 0;
          Unwrap(io_uring_submit(&IOUringEventLoop::holder.ring));
          sqe = io_uring_get_sqe(&IOUringEventLoop::holder.ring);
        }
        sqe->user_data = std::bit_cast<__u64>(&res);
        io_uring_prep_read(sqe, fd, data.data(), data.size(), off);
        ++IOUringEventLoop::holder.cur_in;
        ++IOUringEventLoop::not_submitted_cnt_;
      }

      int await_resume() const noexcept { return res.cnt; }

      int fd;
      std::span<char> data;
      off_t off;
      IOUringEventLoop::ResHolder res;
    };

    co_return co_await ReadAwaitable {
      .fd = fd,
      .data = data,
      .off = off,
    };
  }

  auto Accept() -> Task<File> {
    struct AcceptIPV4Awaitable {
      bool await_ready() const noexcept { return false; }

      void await_suspend(std::coroutine_handle<> handle) {
        res.handle = handle;
        io_uring_sqe *sqe = io_uring_get_sqe(&IOUringEventLoop::holder.ring);
        if (sqe == nullptr) [[unlikely]] {
          IOUringEventLoop::not_submitted_cnt_ = 0;
          Unwrap(io_uring_submit(&IOUringEventLoop::holder.ring));
          sqe = io_uring_get_sqe(&IOUringEventLoop::holder.ring);
        }
        sqe->user_data = std::bit_cast<__u64>(&res);
        io_uring_prep_accept(sqe, fd, reinterpret_cast<sockaddr *>(&client_addr),
                             &client_len, SOCK_NONBLOCK);
        ++IOUringEventLoop::holder.cur_in;
        ++IOUringEventLoop::not_submitted_cnt_;
      }

      int await_resume() const noexcept { return res.cnt; }

      int fd;
      sockaddr_in client_addr;
      socklen_t client_len = sizeof(client_addr);
      IOUringEventLoop::ResHolder res;
    };
    co_return co_await AcceptIPV4Awaitable {
      .fd = fd
    };
  }



  auto Send(std::span<const char> data, int flags) -> Task<size_t> {
    struct SendAwaitable {
      bool await_ready() const noexcept { return false; }

      void await_suspend(std::coroutine_handle<> handle) noexcept {
        res.handle = handle;
        io_uring_sqe *sqe = io_uring_get_sqe(&IOUringEventLoop::holder.ring);
        if (sqe == nullptr) [[unlikely]] {
          IOUringEventLoop::not_submitted_cnt_ = 0;
          Unwrap(io_uring_submit(&IOUringEventLoop::holder.ring));
          sqe = io_uring_get_sqe(&IOUringEventLoop::holder.ring);
        }
        sqe->user_data = std::bit_cast<__u64>(&res);
        io_uring_prep_send(sqe, fd, data.data(), data.size(), flags);
        ++IOUringEventLoop::holder.cur_in;
        ++IOUringEventLoop::not_submitted_cnt_;
      }

      int await_resume() const noexcept { return res.cnt; }

      int fd;
      std::span<const char> data;
      int flags;
      IOUringEventLoop::ResHolder res;
    };
    co_return co_await SendAwaitable{
      .fd = fd,
      .data = data,
      .flags = flags
    };
  }

  auto Write(std::span<const char> data, off_t off) -> Task<size_t> {
    struct WriteAwaitable {
      bool await_ready() const noexcept { return false; }

      void await_suspend(std::coroutine_handle<> handle) noexcept {
        res.handle = handle;
        io_uring_sqe *sqe = io_uring_get_sqe(&IOUringEventLoop::holder.ring);
        if (sqe == nullptr) [[unlikely]] {
          IOUringEventLoop::not_submitted_cnt_ = 0;
          Unwrap(io_uring_submit(&IOUringEventLoop::holder.ring));
          sqe = io_uring_get_sqe(&IOUringEventLoop::holder.ring);
        }
        sqe->user_data = std::bit_cast<__u64>(&res);
        io_uring_prep_write(sqe, fd, data.data(), data.size(), off);
        ++IOUringEventLoop::holder.cur_in;
        ++IOUringEventLoop::not_submitted_cnt_;
      }

      int await_resume() const noexcept { return res.cnt; }

      int fd;
      std::span<const char> data;
      off_t off;
      IOUringEventLoop::ResHolder res;
    };
    co_return co_await WriteAwaitable {
      .fd = fd,
      .data = data,
      .off = off
    };
  }


  auto Recieve(std::span<char> data, int flags = 0) -> Task<size_t> {
    struct RecieveAwaitable {
      bool await_ready() const noexcept { return false; }

      void await_suspend(std::coroutine_handle<> handle) noexcept {
        res.handle = handle;
        io_uring_sqe *sqe = io_uring_get_sqe(&IOUringEventLoop::holder.ring);
        if (sqe == nullptr) [[unlikely]] {
          IOUringEventLoop::not_submitted_cnt_ = 0;
          Unwrap(io_uring_submit(&IOUringEventLoop::holder.ring));
          sqe = io_uring_get_sqe(&IOUringEventLoop::holder.ring);
        }
        sqe->user_data = std::bit_cast<__u64>(&res);
        io_uring_prep_recv(sqe, fd, data.data(), data.size(), flags);
        ++IOUringEventLoop::holder.cur_in;
        ++IOUringEventLoop::not_submitted_cnt_;
      }

      int await_resume() const noexcept { return res.cnt; }

      int fd;
      std::span<char> data;
      int flags;
      IOUringEventLoop::ResHolder res;
    };
    co_return co_await RecieveAwaitable {
      .fd = fd,
      .data = data,
      .flags = flags
    };
  }
  File(int fd)
    : fd(fd) {}

  ~File() {
    if (fd != -1) {
      // close(fd);
    }
  }

  int fd = -1;
};
} // namespace uring