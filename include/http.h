#pragma once

#include "task.h"

#include <io_uring_event_loop.h>

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
  ReqType req_type;
  bool keep_alive = false;
  std::string request_target;
  std::string http_version;
  std::string host;
  std::string body;
};

template<size_t kSz>
class HttpParser {
public:
  HttpParser(int fd) : fd_(fd) {}

  Task<HttpRequest> ParseRequest() {
    HttpRequest ans;
    { // parsing request-line
      std::string_view request_line = co_await GetLine();
      if (std::ranges::count(request_line, ' ') != 2) [[unlikely]] {
        throw std::invalid_argument("http request's request-line must contain exactly 2 spaces");
      }

      // parse method
      size_t method_length = request_line.find_first_of(' ');
      ans.req_type = ParseRequestType(request_line.substr(0, method_length));
      request_line.remove_prefix(method_length + 1);

      // parse request-target
      size_t rt_length = request_line.find_first_of(' ');
      ans.request_target = request_line.substr(0, rt_length);
      request_line.remove_prefix(rt_length + 1);
      ans.http_version = request_line;
    }
    co_return ans;
  }

  Task<std::string_view> GetLine() {
    size_t r_pos = cur_have.find_first_of('\r');
    if (r_pos == cur_have.npos) {
      co_await ReadMore();
      r_pos = cur_have.find_first_of('\r');
      if (r_pos == cur_have.npos) [[unlikely]] {
        throw std::invalid_argument("http request header is longer then buffer");
      }
    }
    std::string_view ans = cur_have.substr(0, r_pos);
    cur_have.remove_prefix(r_pos + 2);
    co_return ans;
  }

  Task<std::string> ReadBody(size_t len) {
    std::string ans;
    while (len > 0) {
      if (cur_have.empty()) {
        co_await ReadMore();
      }
      size_t read_cnt = std::min(len, cur_have.size());
      ans.append_range(cur_have.substr(0, read_cnt));
      cur_have.remove_prefix(read_cnt);
      len -= read_cnt;
    }
    co_return ans;
  }

  Task<> ReadMore() {
    std::ranges::copy(cur_have, buf_.begin());
    cur_have = { buf_.data(), co_await Recieve(fd_, std::span(buf_).subspan(cur_have.size())) };
  }

  void Reconnect(int new_fd) {
    fd_ = new_fd;
    cur_have = {};
  }

private:
  int fd_;
  std::array<char, kSz> buf_;
  std::string_view cur_have;
};