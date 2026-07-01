#pragma once

#include <runtime/task.h>
#include <util/common.hpp>
#include <util/enum_bit_ops.hpp>

#include <string_view>
#include <utility>
#include <span>
#include <system_error>
#include <cstdint>
#include <charconv>

namespace io {
struct SockAddr {
  uint32_t addr;
  uint16_t port;
};

consteval SockAddr operator""_addr(const char* str, std::size_t len) {
  const char* p = str;
  const char* end = str + len;

  auto parse_u32 = [&] (uint32_t max) -> uint32_t {
    uint32_t value{};
    auto [ptr, ec] = std::from_chars(p, end, value);

    if (ec != std::errc{} || ptr == p || value > max) {
      throw "invalid number";
    }

    p = ptr;
    return value;
  };

  uint32_t a = parse_u32(255);
  if (*p++ != '.') throw "expected '.'";

  uint32_t b = parse_u32(255);
  if (*p++ != '.') throw "expected '.'";

  uint32_t c = parse_u32(255);
  if (*p++ != '.') throw "expected '.'";

  uint32_t d = parse_u32(255);
  if (*p++ != ':') throw "expected ':'";

  uint16_t port = parse_u32(65535);

  if (p != end) {
    throw "trailing characters";
  }

  return {
    .addr = (a << 24) |
            (b << 16) |
            (c << 8)  |
            (d << 0),
    .port = port
};
}

struct Socket;


struct SocketView {
public:
  SocketView() = default;

  enum class Access : std::uint8_t {
    kRead = 0b01,
    kWrite = 0b10,
    kRdWr = 0b11,
  };

  auto Poll(Access acc) -> Task<>;
  auto Read(std::span<std::byte> data) -> Task<ssize_t>;
  auto Write(std::span<const std::byte> data) -> Task<ssize_t>;
  auto Accept() -> Task<Socket>;

  void Bind(SockAddr addr);
  void Listen(int backlog = 128);

  int _native_handle() const {
    return fd_;
  }

  friend struct Reactor;
  friend struct Socket;

private:
  int fd_ = -1;

  explicit SocketView(int fd) : fd_(fd) {}
};

struct Socket : MoveOnly, SocketView {
public:
  Socket() = default;

  enum struct Domain {
    kUnix, kInet
  };

  explicit Socket(Domain dom);

  Socket(Socket&& other) noexcept
    : SocketView(std::exchange(other.fd_, -1)) {
  }

  Socket& operator=(Socket&& other) noexcept(false) {
    if (&other == this) {
      return *this;
    }
    Close();
    fd_ = std::exchange(other.fd_, -1);
    return *this;
  }

  ~Socket() {
    Close();
  }


  void Close();

  SocketView View() const {
    return *this;
  }

  friend struct SocketView;

private:
  explicit Socket(int fd) : SocketView(fd) {}
};
}

template<>
inline constexpr bool kEnableEnumBitOps<io::SocketView::Access> = true;
