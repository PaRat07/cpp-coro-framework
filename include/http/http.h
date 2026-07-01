#pragma once

#include "runtime/io/socket.hpp"
#include "runtime/task.h"

#include <fmt/chrono.h>
#include <fmt/compile.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <util/sys/unwrap.h>

#include <unistd.h>


using namespace fmt::literals;
enum class ReqType {
  kGet,
  kPost,
  kPut,
  kDelete,
  kPatch
};



ReqType ParseRequestType(std::string_view sv) {
  if (sv == "GET") {
    return ReqType::kGet;
  } else if (sv == "POST") {
    return ReqType::kPost;
  } else if (sv == "PUT") {
    return ReqType::kPut;
  } else if (sv == "DELETE") {
    return ReqType::kDelete;
  } else if (sv == "PATCH") {
    return ReqType::kPatch;
  } else {
    throw std::invalid_argument(std::format("invalid request type: {}", sv));
  }
}

struct HttpRequest {
  std::string_view method;
  std::string_view path;
  std::string_view version;

  struct Header {
    std::string_view name;
    std::string_view value;
  };
  std::span<Header> headers;
};

class HttpParser {
private:
  static constexpr size_t kBufSz = 4096 * 8;
public:
  HttpParser(io::SocketView &fd) : fd_(fd) {
  }

  // returns if was unable to read all
  bool ReadData() {
    while (read_cnt_ < kBufSz) {
      ssize_t extra_read = read(fd_._native_handle(), req_buf_.data() + read_cnt_, kBufSz - read_cnt_);
      if (extra_read == 0) [[unlikely]] {
        throw std::runtime_error("conn failed");
      } else if (extra_read == -1 && errno == EWOULDBLOCK) {
        return true;
      } else {
        Unwrap(extra_read);
      }
      read_cnt_ += extra_read;
    }
    return false;
  }

  Task<> EachConnectionLoop(std::invocable<std::span<char>, HttpRequest> auto &&func) {
    std::vector<HttpRequest::Header> headers_buf(16);
    while (true) {
      std::span rsp_buf_have(rsp_buf_);
      std::span<char> cur_rsp;
      consumed_cnt_ = 0;
      read_cnt_ = 0;
      bool can_read_more = ReadData();
      while (true) {
        if (auto req = ParseRequest(headers_buf); req.has_value()) {
          std::span<char>::iterator cur_end = co_await func(rsp_buf_have, req.value());
          rsp_buf_have = { cur_end, rsp_buf_have.end() };
          cur_rsp = std::span(rsp_buf_.begin().base(), cur_end.base());
        } else {
          break;
        }
      }
      co_await fd_.Write(std::as_bytes(cur_rsp));
      if (!can_read_more) {
        co_await fd_.Poll(io::SocketView::Access::kRead);
      }
    }
    co_return;
  }

  std::optional<HttpRequest> ParseRequest(std::span<HttpRequest::Header> headers) {
    if (read_cnt_ == consumed_cnt_) {
      return std::nullopt;
    }
    HttpRequest ans;
    size_t cont_length = 0;
    { // parsing request-line
      std::string_view request_line = GetLine();
      if (std::ranges::count(request_line, ' ') != 2) [[unlikely]] {
        return std::nullopt;
        throw std::invalid_argument("http request's request-line must contain exactly 2 spaces");
      }

      // parse method
      size_t method_length = request_line.find_first_of(' ');
      ans.method = request_line.substr(0, method_length);
      request_line.remove_prefix(method_length + 1);

      // parse request-target
      size_t rt_length = request_line.find_first_of(' ');
      ans.path = request_line.substr(0, rt_length);
      request_line.remove_prefix(rt_length + 1);
      ans.version = request_line;
    }
    auto headers_it = headers.begin();
    for (std::string_view line = GetLine(); !line.empty(); line = GetLine()) {
      if (headers_it == headers.end()) [[unlikely]] {
        return std::nullopt;
        throw std::invalid_argument("http request exceeded limit of the headers");
      }
      if (line.starts_with("Content-length: ")) {
        auto subsv = line.substr(line.find_last_of(' ') + 1);
        auto ec = std::from_chars(subsv.begin(), subsv.end(), cont_length).ec;
        if (ec == std::errc::invalid_argument) [[unlikely]] {
          return std::nullopt;
          throw std::invalid_argument("Content-length value is not a number");
        } else if (ec == std::errc::result_out_of_range) [[unlikely]] {
          return std::nullopt;
          throw std::invalid_argument("Content-length value dosn't fit in size_t");
        }
      }
      auto name_length = line.find_first_of(':');
      if (name_length == std::string_view::npos) [[unlikely]] {
        return std::nullopt;
        throw std::invalid_argument("http header line doesnt containt \":\"");
      }
      headers_it->name = line.substr(0, name_length);
      line.remove_prefix(name_length + 1);
      headers_it->value = line.substr(line.find_first_not_of(' '));
      ++headers_it;
    }
    ans.headers = { headers.begin(), headers_it };
    return ans;
  }

  std::string_view GetLine() {
    size_t r_pos;
    if ((r_pos = GetCurHaveSv().find_first_of('\r')) == std::string_view::npos) {
      throw std::runtime_error("idk");
    }
    std::string_view ans = GetCurHaveSv().substr(0, r_pos);
    consumed_cnt_ += r_pos + 2;
    return ans;
  }

private:
  io::SocketView fd_;
  size_t read_cnt_ = 0;
  size_t consumed_cnt_ = 0;
  std::vector<char> req_buf_ = std::vector<char>(kBufSz);
  std::vector<char> rsp_buf_ = std::vector<char>(kBufSz);


  std::string_view GetCurHaveSv() const {
    return { req_buf_.data() + consumed_cnt_, req_buf_.data() + read_cnt_ };
  }
};


std::string ToString(int num) {
  std::array<char, 10> ans;
  return std::string(ans.begin(), std::to_chars(ans.begin(), ans.end(), num).ptr - ans.begin());
}

int DigCnt(int num) {
  int ans = 0;
  while (num > 0) {
    ++ans;
    num /= 10;
  }
  return ans;
}


std::span<char>::iterator WriteResponse(std::span<char> output, io::SocketView &fd, std::span<const std::pair<std::string_view, std::string_view>> headers, std::string_view body) {
  using namespace std::string_view_literals;
  namespace rng = std::ranges;
  namespace chr = std::chrono;

  auto it = output.begin();
  it = rng::copy("HTTP/1.1 200 OK\r\n"sv, it).out;
  it = rng::copy("Content-Length: "sv, it).out;
  auto buf = ToString(body.size());
  it = rng::copy(buf, it).out;
  it = rng::copy("\r\n"sv, it).out;
  it = rng::copy("Date: "sv, it).out;
  it = fmt::format_to(it, "{:%a, %d %b %Y %H:%M:%S GMT}\r\n"_cf, chr::floor<chr::seconds>(chr::system_clock::now()));
  for (auto [key, val] : headers) {
    it = rng::copy(key, it).out;
    it = rng::copy(": "sv, it).out;
    it = rng::copy(val, it).out;
    it = rng::copy("\r\n"sv, it).out;
  }
  it = rng::copy("\r\n"sv, it).out;
  it = rng::copy(body, it).out;
  return it;
}
