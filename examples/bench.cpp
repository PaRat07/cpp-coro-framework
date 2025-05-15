#include <http.h>
#include <chrono>
#include "io_uring_event_loop.h"

#include "coro_utility.h"
#include "main_task.h"
#include "task.h"
#include "timed_event_loop.h"
#include <sys/socket.h>
#include "errno.h"
#include <thread>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace std::string_view_literals;

constexpr std::string_view hello_world_text =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 15\r\n"
    "Content-Type: text/plain; charset=UTF-8\r\n"
    "Server: Example\r\n"
    "Date: {:%a, %d %b %Y %H:%M:%S GMT}\r\n"
    "Connection: keep-alive\r\n\r\n"
    "Hello, world!\r\n";
auto Loop(int fd) -> Task<> {
    std::cout << "Started" << std::endl;
    std::array<char, 1024> resp_buf;
    HttpParser<1024> parser(-1);
    while (true) {
        int connfd = co_await AcceptIPV4(fd);
        bool reuse_connection = true;
        parser.Reconnect(connfd);
        try {
            while (reuse_connection || true) {
                HttpRequest req = co_await parser.ParseRequest();
                reuse_connection = req.keep_alive;
                co_await SendResponse(connfd, resp_buf, {
                    {
                        { "Content-Type", "text/plain; charset=UTF-8" },
                        { "Server", "Example" },
                        { "Connection", "keep-alive" }
                    },
                    "Hello, world!"
                });
                // co_await Write(connfd, std::format(hello_world_text, std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now())), 0);
            }
        } catch (...) {
            close(connfd);
            std::cerr << "Failed" << std::endl;

        }
    }
}


MainTask co_server(int fd) {
    std::array<Task<>, 3'000> tasks;
    for (int i = 0; i < tasks.size(); ++i) {
        tasks[i] = Loop(fd);
    }
    co_await WhenAll(tasks);

    co_return;
}

int main() {
    signal(SIGPIPE, SIG_IGN);
    // std::cout << std::format("{}", std::chrono::system_clock::now()) << std::endl;
    // return 0;
    // co_server(/*std::make_index_sequence<10>{}*/).RunLoop<IOUringEventLoop>();

    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);

    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        throw std::system_error(errno, std::system_category(), "setsockopt SO_REUSEADDR");
    }
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        throw std::system_error(errno, std::system_category(), "setsockopt SO_REUSEPORT");
    }
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)); // Disable Nagle's algorithm
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));  // Keep connections alive


    sockaddr_in addr;
    addr.sin_family = AF_INET;          // IPv4
    addr.sin_addr.s_addr = INADDR_ANY;  // 0.0.0.0
    addr.sin_port = htons(8080);        // Port 8888
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        throw std::system_error(errno, std::system_category(), "bind error");
    }

    if (listen(fd, SOCK_STREAM) < 0) {
        close(fd);
        throw std::system_error(errno, std::system_category(), "listen error");
    }
    fork();fork();fork();fork();
    co_server(fd).RunLoop<IOUringEventLoop>();
}



// wrk -H 'Host: tfb-server' -H 'Accept: text/plain,text/html;q=0.9,application/xhtml+xml;q=0.9,application/xml;q=0.8,*/*;q=0.7' -H 'Connection: keep-alive' --latency -d 15 -c 16384 --timeout 8 -t 16 http://localhost:8080/plaintext -s pipeline.lua -- 16
