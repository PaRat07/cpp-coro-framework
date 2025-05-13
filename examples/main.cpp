#include <http.h>
#include <chrono>
#include "io_uring_event_loop.h"

#include "coro_utility.h"
#include "main_task.h"
#include "task.h"
#include "timed_event_loop.h"

#include "errno.h"

using namespace std::chrono_literals;

std::ostream &operator<<(std::ostream &out, std::u8string_view u8sv) {
    out << std::string_view(std::bit_cast<char*>(u8sv.data()), u8sv.size());
    return out;
}

Task<int> Get(int x) noexcept {
    std::println("Before: {}", x);
    co_await SleepFor(1s);
    std::println("After: {}", x);
    co_return x;
}

Task<std::u8string> TestUring() {
    std::u8string_view text = u8"goyda";
    int fd = open("/tmp/test.txt", O_CREAT | O_TRUNC | O_RDWR);
    if (co_await Write(fd, text, 0) != text.size()) {
        throw std::runtime_error("unable to write to file");
    }
    std::array<char8_t, 50> buf;
    size_t read_cnt = co_await Read(fd, buf, 0);
    co_return { buf.data(), read_cnt };
}


Task<int> Print() noexcept {
    std::println("Got: {}", co_await Get(13));
    co_return 1;
}

MainTask co_main() {
    std::apply([] (auto... args) {
        ((std::cout << args << std::endl), ...);
    }, co_await WhenAll(Print(), Print(), TestUring()));
    co_return;
}


constexpr std::string_view hello_world_text =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 15\r\n"
    "Content-Type: text/plain; charset=UTF-8\r\n"
    "Server: Example\r\n"
    "Date: {:%a, %d %b %Y %H:%M:%S GMT}\r\n"
    "Connection: keep-alive\r\n\r\n"
    "Hello, world!\r\n";

auto Loop(int fd) -> Task<> {
    int connfd = co_await AcceptIPV4(fd);
    bool reuse_connection = true;
    HttpParser<100> parser(connfd);
    while (true) {
        if (!reuse_connection) {
            connfd = co_await AcceptIPV4(fd);
            parser.Reconnect(connfd);
        }
        HttpRequest req = co_await parser.ParseRequest();
        reuse_connection = req.keep_alive;
        co_await Write(connfd, std::format(hello_world_text, std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now())), 0)
    }
}


template<size_t... Inds>
MainTask co_server(std::index_sequence<Inds...>) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);

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
    co_await WhenAll(Loop((Inds, fd))...);
    co_return;
}

int main() {
    // std::cout << std::format("{}", std::chrono::system_clock::now()) << std::endl;
    // return 0;
    co_server(std::make_index_sequence<50>{}).RunLoop<TimedEventLoop, IOUringEventLoop>();
}

