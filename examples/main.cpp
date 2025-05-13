#include <http.h>
#include <chrono>
#include "io_uring_event_loop.h"

#include "coro_utility.h"
#include "main_task.h"
#include "task.h"
#include "timed_event_loop.h"

#include "errno.h"
#include <thread>

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace std::string_view_literals;

Task<int> Get(int x) noexcept {
    std::println("Before: {}", x);
    co_await SleepFor(1s);
    std::println("After: {}", x);
    co_return x;
}

Task<std::string> TestUring() {
    std::string_view text = "goyda";
    int fd = open("/tmp/test.txt", O_CREAT | O_TRUNC | O_RDWR);
    if (co_await Write(fd, text, 0) != text.size()) {
        throw std::runtime_error("unable to write to file");
    }
    std::array<char, 50> buf;
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

// Task<> SendResponse(int fd, HttpResponse resp) {
//     size_t sz = "HTTP/1.1 200 OK\r\n"sv.size();
//     sz += "Content-Length: "sv.size() + DigCnt(resp.body.size()) + 2;
//     for (auto &&[key, val] : resp.headers) {
//         sz += key.size() + 2 + val.size() + 2;
//     }
//     std::string resp_str;
//     resp_str.reserve(sz);
//     resp_str += "HTTP/1.1 200 OK\r\n";
//     for (auto [key, val] : resp.headers) {
//         resp_str += std::format("{}: {}\r\n", key, val);
//     }
//     resp_str += "\r\n" + resp.body;
//     co_await Write(fd, resp_str, 0);
//     co_return;
// }

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
    while (true) {
        int connfd = co_await AcceptIPV4(fd);
        bool reuse_connection = true;
        HttpParser<1024> parser(connfd);
        try {
            while (reuse_connection || true) {
                // std::array<char, 1024> buf;
                // co_await
                HttpRequest req = co_await parser.ParseRequest();
                reuse_connection = req.keep_alive;
                // co_await SendResponse(fd, {
                //     {
                //         "Content-Type",
                //     },
                //     "Hello, world!"
                // });
                co_await Write(connfd, std::format(hello_world_text, std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now())), 0);

            }
        } catch (...) {
            std::cerr << "Failed" << std::endl;
        }
    }
}


template<size_t... Inds>
MainTask co_server(int fd, std::index_sequence<Inds...>) {
    // std::array<Task<>, 10'000> tasks;
    // for (int i = 0; i < 10'000; ++i) {
    //     tasks[i] = Loop(fd);
    // }
    // co_await WhenAll(tasks);
    // co_await Loop(fd);

    co_await WhenAll([fd] <size_t... InInds> (std::index_sequence<InInds...>) -> Task<> {
        co_await WhenAll(Loop((InInds, fd))...);
        co_return;
    } (std::make_index_sequence<(Inds, 200)>{})...);
    co_return;
}

int main() {
    // std::cout << std::format("{}", std::chrono::system_clock::now()) << std::endl;
    // return 0;
    // co_server(/*std::make_index_sequence<10>{}*/).RunLoop<IOUringEventLoop>();

    int fd = socket(AF_INET, SOCK_STREAM, 0);

    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        throw std::system_error(errno, std::system_category(), "setsockopt SO_REUSEADDR");
    }

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
    co_server(fd, std::make_index_sequence<10>{}).RunLoop<IOUringEventLoop>();
}

