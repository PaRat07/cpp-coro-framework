#include <runtime/io/reactor.hpp>

#include "util/sys/unwrap.h"

#include <array>
#include <system_error>

#include <sys/epoll.h>
#include <unistd.h>

static int Unwrap(int res) {
  if (res == -1) [[unlikely]] {
    throw std::system_error(errno, std::system_category());
  }
  return res;
}

io::Reactor::Reactor()
  : epoll_fd_(Unwrap(epoll_create1(EPOLL_CLOEXEC))) {
}

io::Reactor::~Reactor() {
  Unwrap(close(epoll_fd_));
}

static uint32_t ToEpollAccess(io::SocketView::Access acc) {
  uint32_t res = EPOLLONESHOT;
  if (std::to_underlying(acc & io::SocketView::Access::kRead)) {
    res |= EPOLLIN;
  }
  if (std::to_underlying(acc & io::SocketView::Access::kWrite)) {
    res |= EPOLLOUT;
  }
  return res;
}

void io::Reactor::Watch(SocketView sock, SocketView::Access wait_for,
                   std::coroutine_handle<> awaiter) {
  epoll_event ev{
    .events = ToEpollAccess(wait_for),
    .data{.ptr = awaiter.address()},
};

  int op = EPOLL_CTL_MOD;

  if (epoll_ctl(epoll_fd_, op, sock.fd_, &ev) == -1) {
    if (errno == ENOENT) {
      op = EPOLL_CTL_ADD;

      if (epoll_ctl(epoll_fd_, op, sock.fd_, &ev) == -1) {
        ThrowFromErrno();
      }
    } else {
      ThrowFromErrno();
    }
  }
}

void io::Reactor::Poll() {
  std::array<epoll_event, 128> events;
  while (true) {
    int ev_cnt = epoll_wait(epoll_fd_, events.data(), events.size(), -1);
    if (ev_cnt == -1) [[unlikely]] {
      ThrowFromErrno();
    }
    for (const epoll_event& ev : std::span(events.data(), ev_cnt)) {
      std::coroutine_handle<>::from_address(ev.data.ptr).resume();
    }
  }
}

