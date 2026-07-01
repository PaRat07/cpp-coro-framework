#include <runtime/io/socket.hpp>
#include <sys/socket.h>
#include <runtime/io/reactor.hpp>

#include "ctre.hpp"

#include <util/sys/unwrap.h>

#include <netinet/in.h>
#include <unistd.h>

using namespace io;

auto SocketView::Poll(Access acc) -> Task<> {
  struct PollAwaitable {
    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle) {
      Reactor::Get().Watch(sock, acc, handle);
    }

    void await_resume() const noexcept {}

    Access acc;
    SocketView sock;
  };
  co_await PollAwaitable {
    .acc = acc,
    .sock = *this
  };
  co_return;
}

auto SocketView::Read(std::span<std::byte> data) -> Task<ssize_t> {
  ssize_t res;
  while ((res = read(fd_, data.data(), data.size())) == -1) {
    if (errno != EWOULDBLOCK && errno != EAGAIN) {
      ThrowFromErrno();
    }
    co_await Poll(Access::kRead);
  }
  co_return res;
}

auto SocketView::Write(std::span<const std::byte> data) -> Task<ssize_t> {
  ssize_t res;
  while ((res = write(fd_, data.data(), data.size())) == -1) {
    if (errno != EWOULDBLOCK && errno != EAGAIN) {
      ThrowFromErrno();
    }
    co_await Poll(Access::kWrite);
  }
  co_return res;
}

auto SocketView::Accept() -> Task<Socket> {
  int fd;
  while ((fd = accept4(fd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC)) == -1) {
    if (errno != EWOULDBLOCK && errno != EAGAIN) {
      ThrowFromErrno();
    }
    co_await Poll(Access::kRead);
  }
  co_return Socket(fd);
}

void SocketView::Bind(SockAddr addr) {
  sockaddr_in sa{};

  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = addr.addr;
  sa.sin_port = htons(addr.port);

  Unwrap(bind(fd_, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)));
}
void SocketView::Listen(int backlog) {
  Unwrap(listen(fd_, backlog));
}

static int ToNative(Socket::Domain dom) {
  switch (dom) {
  case Socket::Domain::kInet:
    return AF_INET;
  case Socket::Domain::kUnix:
    return AF_UNIX;
  default:
    std::terminate();
  }
}

Socket::Socket(Domain dom)
  : SocketView(Unwrap(socket(ToNative(dom), SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0))){
  int yes = 1;
  Unwrap(setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)));
}

void Socket::Close() {
  if (fd_ != -1) {
    Unwrap(close(std::exchange(fd_, -1)));
  }
}




