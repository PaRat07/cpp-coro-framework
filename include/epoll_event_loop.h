#pragma once

#include <sys/epoll.h>
#include "sys_utility.h"

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

namespace epoll {
namespace chr = std::chrono;
using namespace std::chrono_literals;

struct EpollHolder {
  void Init() {
    epoll_fd = Unwrap(epoll_create1(0));
  }

  ~EpollHolder() {
    close(epoll_fd);
  }
  int epoll_fd;
};

class EpollEventLoop {
 public:
  static void Resume() noexcept {
    int nready = Unwrap(epoll_wait(holder_.epoll_fd, events_.data(), events_.size(), /*timeout_ms=*/-1));
    for (auto &&i : events_ | std::views::take(nready)) {
      std::coroutine_handle<>::from_address(i.data.ptr).resume();
    }
    if (nready == events_.size()) {
      events_.resize(events_.size() * 2);
    }
  }

  static void Init() {
    holder_.Init();
    events_.resize(32);
  }


  struct ReadAwaitable {
    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle) {
      epoll_event ev{};
      ev.events = EPOLLIN | EPOLLONESHOT;
      ev.data.ptr = handle.address();
      Unwrap(epoll_ctl(holder_.epoll_fd, (armed ? EPOLL_CTL_MOD : EPOLL_CTL_ADD), fd, &ev));
    }

    void await_resume() const noexcept {}

    int fd;
    bool armed;
  };

  struct WriteAwaitable {
    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle) noexcept {
      epoll_event ev{};
      ev.events = EPOLLOUT | EPOLLONESHOT;
      ev.data.ptr = handle.address();
      Unwrap(epoll_ctl(holder_.epoll_fd, (armed ? EPOLL_CTL_MOD : EPOLL_CTL_ADD), fd, &ev));
    }

    void await_resume() const noexcept {}

    int fd;
    bool armed;
  };

  struct PollAwaitable {
    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle) {
      epoll_event ev{};
      ev.events = EPOLLIN | EPOLLOUT | EPOLLONESHOT;
      ev.data.ptr = handle.address();
      Unwrap(epoll_ctl(holder_.epoll_fd, (armed ? EPOLL_CTL_MOD : EPOLL_CTL_ADD), fd, &ev));
    }

    void await_resume() const noexcept {}

    int fd;
    bool armed;
  };
  friend struct File;

 private:
  static inline std::vector<epoll_event> events_;
  static inline EpollHolder holder_;
};
  
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

struct File {
  auto Read(std::span<char> data, off_t off) -> Task<size_t> {
    bool buf = armed;
    armed = true;
    co_await EpollEventLoop::ReadAwaitable {
      .fd = fd,
      .armed = buf
    };
    lseek(fd, off, SEEK_SET);
    co_return Unwrap(read(fd, data.data(), data.size()));
  }

  auto Accept() -> Task<File> {
    bool buf = armed;
    armed = true;
    co_await EpollEventLoop::PollAwaitable {
      .fd = fd,
      .armed = buf
    };
    sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    co_return File(accept4(fd, reinterpret_cast<sockaddr *>(&client_addr), &client_len, SOCK_NONBLOCK));
  }



  auto Send(std::span<const char> data, int flags) -> Task<size_t> {
    bool buf = armed;
    armed = true;
    co_await EpollEventLoop::WriteAwaitable {
      .fd = fd,
      .armed = buf
    };
    co_return Unwrap(send(fd, data.data(), data.size(), flags));
  }

  auto Write(std::span<const char> data, off_t off) -> Task<size_t> {
    bool buf = armed;
    armed = true;
    co_await EpollEventLoop::WriteAwaitable {
      .fd = fd,
      .armed = buf
    };
    lseek(fd, off, SEEK_SET);
    co_return Unwrap(write(fd, data.data(), data.size()));
  }


  auto Recieve(std::span<char> data, int flags = 0) -> Task<size_t> {
    bool buf = armed;
    armed = true;
    co_await EpollEventLoop::ReadAwaitable {
      .fd = fd,
      .armed = buf
    };
    co_return Unwrap(recv(fd, data.data(), data.size(), flags));
  }
  File(const File &rhs) = delete;
  File(File &&rhs)
    : fd(std::exchange(rhs.fd, -1)),
      armed(std::exchange(rhs.armed, false)) {
  }
  File(int fd) : fd(fd) {
  }

  File &operator=(const File&) = delete;

  File &operator=(File &&rhs) {
    fd = std::exchange(rhs.fd, -1);
    armed = std::exchange(rhs.armed, false);
  }
  File() = default;

  ~File() {
    if (fd != -1) {
      if (armed) {
        Unwrap(epoll_ctl(EpollEventLoop::holder_.epoll_fd, EPOLL_CTL_DEL, fd, nullptr));
      }
      close(fd);
    }
  }

  int fd = -1;
  bool armed = false;
};
} // namespace epoll