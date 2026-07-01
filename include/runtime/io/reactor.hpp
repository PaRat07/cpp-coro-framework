#pragma once

#include <runtime/io/socket.hpp>

#include <util/common.hpp>

namespace io {
struct Reactor : Immovable {
public:
  Reactor();
  ~Reactor();

  void Watch(io::SocketView sock, SocketView::Access wait_for, std::coroutine_handle<> awaiter);
  void Poll();

  static Reactor& Get() {
    static thread_local Reactor reactor;
    return reactor;
  }

private:
  int epoll_fd_;
};
}