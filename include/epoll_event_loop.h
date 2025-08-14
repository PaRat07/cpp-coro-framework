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
#include <netinet/in.h>
#include <sys/socket.h>

#include "task.h"
#include "coro_utility.h"
#include <memory>

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
  struct EpollWaiter {
    uint32_t events;
    std::coroutine_handle<> handle;
  };

  static void Resume() noexcept {
    int nready = Unwrap(epoll_wait(holder_.epoll_fd, events_.data(), events_.size(), /*timeout_ms=*/-1));
    for (auto i : events_ | std::views::take(nready)) {
      auto *waiter = std::bit_cast<EpollWaiter*>(i.data.ptr);
      waiter->events = i.events;
      waiter->handle.resume();
    }
  }

  static void Init() {
    holder_.Init();
  }


  friend struct File;

 private:
  static inline std::array<epoll_event, 1024> events_;
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
private:
  struct SharedData {
    bool alive = true;
    Queue<std::coroutine_handle<>> on_read;
    Queue<std::coroutine_handle<>> on_write;
  };


  struct EpollAwaitable {
    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle) {
      (is_read ? sh_data.on_read : sh_data.on_write).Push(handle);
    }

    void await_resume() const noexcept {}

    bool is_read;
    SharedData &sh_data;
  };

public:
  auto Read(std::span<char> data) -> Task<size_t> {
    int cnt;
    while ((cnt = read(fd, data.data(), data.size())) == -1) {
      if (errno != EWOULDBLOCK) [[unlikely]] {
        throw std::system_error(errno, std::system_category(), "error reading from fd");
      }
      co_await Poll(true);
    }
    co_return cnt;
  }

  auto Accept() -> Task<File> {
    int sock;
    sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    while ((sock = accept4(fd, reinterpret_cast<sockaddr *>(&client_addr), &client_len, SOCK_NONBLOCK)) == -1) {
      if (errno != EWOULDBLOCK) [[unlikely]] {
        throw std::system_error(errno, std::system_category(), "error accepting at fd");
      }
      co_await Poll(true);
    }
    co_return sock;
  }

  auto Write(std::span<const char> data) -> Task<size_t> {
    int cnt;
    while ((cnt = write(fd, data.data(), data.size())) == -1) {
      if (errno != EWOULDBLOCK) [[unlikely]] {
        throw std::system_error(errno, std::system_category(), "error writing from fd");
      }
      co_await Poll(false);
    }
    co_return cnt;
  }

  auto Poll(bool is_read) -> Task<> {
    co_await EpollAwaitable {
      .is_read = is_read,
      .sh_data = *sh_data
    };
    co_return;
  }

  File(const File &rhs) = delete;
  File(File &&rhs) = default;

  File &operator=(const File&) = delete;
  File &operator=(File &&rhs) = default;

  File(int fd_val) {
    fd = fd_val;
    spawn([] (std::shared_ptr<SharedData> sh_data, int fd) -> Task<> {
      EpollEventLoop::EpollWaiter waiter {
        .handle = co_await Self()
      };
      {
        epoll_event event {
          .events = EPOLLIN | EPOLLOUT | EPOLLERR,
          .data = std::bit_cast<epoll_data_t>(&waiter)
       };
       Unwrap(epoll_ctl(EpollEventLoop::holder_.epoll_fd, EPOLL_CTL_ADD, fd, &event));
      }
      while (true) {
        co_await std::suspend_always{};
        if (waiter.events & EPOLLIN) {
          if (!sh_data->on_read.Empty()) {
            sh_data->on_read.Pop().resume();
          }
        }
        if (waiter.events & EPOLLOUT) {
          if (!sh_data->on_write.Empty()) {
            sh_data->on_write.Pop().resume();
          }
        }

        if (waiter.events & EPOLLERR || !sh_data->alive) {
          while (!sh_data->on_read.Empty()) {
            sh_data->on_read.Pop().resume();
          }
          while (!sh_data->on_write.Empty()) {
            sh_data->on_write.Pop().resume();
          }
          Unwrap(epoll_ctl(EpollEventLoop::holder_.epoll_fd, EPOLL_CTL_DEL, fd, nullptr));
          co_return;
        }
      }
      co_return;
    } (sh_data, fd));
  }

  ~File() {
    if (sh_data) {
      sh_data->alive = false;
    }
  }

private:
  int fd;
  std::shared_ptr<SharedData> sh_data = std::make_shared<SharedData>();
};
} // namespace epoll