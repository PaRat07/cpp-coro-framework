#include <chrono>
#include <http/http.h>

#include "errno.h"
#include "runtime/sync/coro_utility.h"
#include "runtime/sync/main_task.h"
#include "runtime/task.h"
#include <iostream>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <rfl.hpp>
#include <rfl/json.hpp>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace std::string_view_literals;

// struct InvokeOnConstruct {
//   InvokeOnConstruct(auto &&f) { f(); }
// };
//
// #define CONCAT_IMPL(a, b) a##b
// #define CONCAT(a, b) CONCAT_IMPL(a, b)
// #define ONCE static InvokeOnConstruct CONCAT(unique_name, __LINE__) = [&]

auto ProcConn(io::Socket connfd) -> Task<> {
  HttpParser parser(connfd);
  try {
    co_await parser.EachConnectionLoop(
        [&](std::span<char> rsp_buf,
            HttpRequest req) -> Task<std::span<char>::iterator> {
          if (req.path == "/plaintext") {
            co_return WriteResponse(
                rsp_buf, connfd,
                std::array{
                    std::pair{"Content-Type"sv, "text/plain; charset=UTF-8"sv},
                    std::pair{"Server"sv, "Example"sv},
                    std::pair{"Connection"sv, "keep-alive"sv}},
                "Hello, world!");
          } else if (req.path == "/json") {
            struct JsonResp {
              std::string_view message;
            };
            std::string body =
                rfl::json::write(JsonResp{.message = "Hello, World!"});
            co_return WriteResponse(
                rsp_buf, connfd,
                std::to_array<std::pair<std::string_view, std::string_view>>({
                  { "Content-Type", "application/json; charset=UTF-8" },
                  { "Server", "Example" },
                  { "Connection", "keep-alive" }
                }),
                body);
          } else {
            throw std::runtime_error("incorrect prefix");
          }
        });
  } catch (const std::exception &exc) {
    std::cerr << "Request error: " << exc.what() << std::endl;
  }
  co_return;
}


MainTask co_server(io::Socket sock) {
  while (true) {
    spawn(ProcConn(co_await sock.Accept()));
  }
  co_return;
}

int main() {
  signal(SIGPIPE, SIG_IGN);

  io::Socket sock(io::Socket::Domain::kInet);
  using io::operator""_addr;
  sock.Bind("0.0.0.0:5832"_addr);
  sock.Listen();

  co_server(std::move(sock)).RunLoop();
}

// wrk -H 'Host: tfb-server' -H 'Accept:
// text/plain,text/html;q=0.9,application/xhtml+xml;q=0.9,application/xml;q=0.8,*/*;q=0.7'
// -H 'Connection: keep-alive' --latency -d 15 -c 16384 --timeout 8 -t 16
// http://localhost:8080/plaintext -s pipeline.lua -- 16 wrk -H 'Host:
// tfb-server' -H 'Accept:
// text/plain,text/html;q=0.9,application/xhtml+xml;q=0.9,application/xml;q=0.8,*/*;q=0.7'
// -H 'Connection: keep-alive' --latency -d 15 -c 1024  --timeout 8 -t 1
// http://localhost:8080/plaintext -s pipeline.lua -- 16 wrk -H 'Host:
// tfb-server' -H 'Accept:
// text/plain,text/html;q=0.9,application/xhtml+xml;q=0.9,application/xml;q=0.8,*/*;q=0.7'
// -H 'Connection: keep-alive' --latency -d 15 -c 1  --timeout 8 -t 1
// http://localhost:8080/plaintext -s pipeline.lua -- 16

// wrk -H 'Host: tfb-server' -H 'Accept:
// application/json,text/html;q=0.9,application/xhtml+xml;q=0.9,application/xml;q=0.8,*/*;q=0.7'
// -H 'Connection: keep-alive' --latency -d 15 -c 512 --timeout 8 -t 16
// "http://localhost:8080/db" wrk -H 'Host: tfb-server' -H 'Accept:
// application/json,text/html;q=0.9,application/xhtml+xml;q=0.9,application/xml;q=0.8,*/*;q=0.7'
// -H 'Connection: keep-alive' --latency -d 15 -c 32 --timeout 8 -t 1
// "http://localhost:8080/db" wrk -H 'Host: tfb-server' -H 'Accept:
// application/json,text/html;q=0.9,application/xhtml+xml;q=0.9,application/xml;q=0.8,*/*;q=0.7'
// -H 'Connection: keep-alive' --latency -d 1 -c 1 --timeout 8 -t 1
// "http://localhost:8080/db" curl -H 'Host: tfb-server' -H 'Accept:
// application/json,text/html;q=0.9,application/xhtml+xml;q=0.9,application/xml;q=0.8,PGRES_COMMAND_OK*/*;q=0.7'
// -H 'Connection: keep-alive' "http://localhost:8080/db"