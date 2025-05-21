#pragma once

#include <fmt/chrono.h>
#include "task.h"

#include <chrono>
#include <io_uring_event_loop.h>
#include <charconv>
#include <algorithm>

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
  bool keep_alive = true;
  std::string request_target;
  std::string http_version;
  std::string host;
  std::string body;
};

template<size_t kSz>
class HttpParser {
public:
  HttpParser(File &fd) : fd_(&fd) {}

  Task<HttpRequest> ParseRequest() {
    HttpRequest ans;
    size_t cont_length = 0;
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
    for (std::string_view line = co_await GetLine(); !line.empty(); line = co_await GetLine()) {
      if (line.starts_with("Connection: ")) {
        ans.keep_alive = line.ends_with("keep-alive");
      } else if (line.starts_with("Content-length: ")) {
        auto subsv = line.substr(line.find_last_of(' ') + 1);
        auto ec = std::from_chars(subsv.begin(), subsv.end(), cont_length).ec;
        if (ec == std::errc::invalid_argument) [[unlikely]] {
          throw std::invalid_argument("Content-length value is not a number");
        } else if (ec == std::errc::result_out_of_range) [[unlikely]] {
          throw std::invalid_argument("Content-length value dosn't fit in size_t");
        }
      }
    }
    ans.body = co_await ReadBody(cont_length);
    co_return ans;
  }

  Task<std::string_view> GetLine() {
    size_t r_pos;
    size_t cnt = 0;
    while ((r_pos = cur_have.find_first_of('\r')) == cur_have.npos) {
      co_await ReadMore();
      ++cnt;
      if (cnt > 5) {
        throw std::runtime_error("connection failed");
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
    cur_have = { buf_.data(), cur_have.size() + co_await fd_->Recieve(std::span(buf_).subspan(cur_have.size())) };
  }

  void Reconnect(File &new_fd) {
    fd_ = &new_fd;
    cur_have = {};
  }

private:
  File *fd_;
  std::array<char, kSz> buf_;
  std::string_view cur_have;
};

struct HttpResponse {
  std::vector<std::pair<std::string_view, std::string>> headers;
  std::string body;
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

Task<> SendResponse(File &fd, std::span<char> storage, HttpResponse resp) {
  using namespace std::string_view_literals;
  namespace rng = std::ranges;
  namespace chr = std::chrono;

  auto it = storage.begin();
  it = rng::copy("HTTP/1.1 200 OK\r\n"sv, it).out;
  it = rng::copy("Content-Length: "sv, it).out;
  auto buf = ToString(resp.body.size());
  it = rng::copy(buf, it).out;
  it = rng::copy("\r\n"sv, it).out;
  it = rng::copy("Date: "sv, it).out;
  it = fmt::format_to(it, "{:%a, %d %b %Y %H:%M:%S GMT}\r\n"sv, chr::floor<chr::seconds>(chr::system_clock::now()));
  for (auto &[key, val] : resp.headers) {
    it = rng::copy(key, it).out;
    it = rng::copy(": "sv, it).out;
    it = rng::copy(val, it).out;
    it = rng::copy("\r\n"sv, it).out;
  }
  it = rng::copy("\r\n"sv, it).out;
  it = rng::copy(resp.body, it).out;
  co_await fd.Send(storage.subspan(0, it - storage.begin()), 0);
  co_return;
}
