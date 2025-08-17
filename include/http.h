#pragma once

#include <fmt/chrono.h>
#include <fmt/compile.h>
#include "task.h"

#include <chrono>
#include <charconv>
#include <algorithm>

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
public:
  HttpParser(File &fd) : fd_(&fd) {}

  Task<HttpRequest> ParseRequest(std::span<HttpRequest::Header> headers) {
    HttpRequest ans;
    size_t cont_length = 0;
    if (GetCurHaveSv().find("\r\n\r\n") == std::string_view::npos) {
      std::ranges::copy(GetCurHaveSv(), data_.begin());
      read_cnt_ -= consumed_cnt_;
      consumed_cnt_ = 0;
      size_t retry_cnt = 0;
      do {
        if (retry_cnt > 5) [[unlikely]] {
          throw std::runtime_error("conn failed");
        }
        read_cnt_ += co_await fd_->Read(std::span(data_).subspan(read_cnt_));
        ++retry_cnt;
      } while (GetCurHaveSv().find("\r\n\r\n") == std::string_view::npos);
    }
    { // parsing request-line
      std::string_view request_line = co_await GetLine();
      if (std::ranges::count(request_line, ' ') != 2) [[unlikely]] {
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
    for (std::string_view line = co_await GetLine(); !line.empty(); line = co_await GetLine()) {
      if (headers_it == headers.end()) [[unlikely]] {
        throw std::invalid_argument("http request exceeded limit of the headers");
      }
      if (line.starts_with("Content-length: ")) {
        auto subsv = line.substr(line.find_last_of(' ') + 1);
        auto ec = std::from_chars(subsv.begin(), subsv.end(), cont_length).ec;
        if (ec == std::errc::invalid_argument) [[unlikely]] {
          throw std::invalid_argument("Content-length value is not a number");
        } else if (ec == std::errc::result_out_of_range) [[unlikely]] {
          throw std::invalid_argument("Content-length value dosn't fit in size_t");
        }
      }
      auto name_length = line.find_first_of(':');
      if (name_length == std::string_view::npos) [[unlikely]] {
        throw std::invalid_argument("http header line doesnt containt \":\"");
      }
      headers_it->name = line.substr(0, name_length);
      line.remove_prefix(name_length + 1);
      headers_it->value = line.substr(line.find_first_not_of(' '));
      ++headers_it;
    }
    ans.headers = { headers.begin(), headers_it };
    co_return ans;
  }

  Task<std::string_view> GetLine() {
    size_t r_pos;
    if ((r_pos = GetCurHaveSv().find_first_of('\r')) == std::string_view::npos) {
      throw std::runtime_error("idk");
    }
    std::string_view ans = GetCurHaveSv().substr(0, r_pos);
    consumed_cnt_ += r_pos + 2;
    co_return ans;
  }

private:
  File *fd_;
  size_t read_cnt_ = 0;
  size_t consumed_cnt_ = 0;
  std::vector<char> data_ = std::vector<char>(1024);


  std::string_view GetCurHaveSv() const {
    return { data_.data() + consumed_cnt_, data_.data() + read_cnt_ };
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


Task<> SendResponse(File &fd, std::span<char> storage, std::span<const std::pair<std::string_view, std::string_view>> headers, std::string_view body) {
  using namespace std::string_view_literals;
  namespace rng = std::ranges;
  namespace chr = std::chrono;

  auto it = storage.begin();
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
  co_await fd.Write(storage.subspan(0, it - storage.begin()));
  co_return;
}
