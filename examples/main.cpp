#include "io_uring_event_loop.h"
#include <chrono>

#include "task.h"
#include "timed_event_loop.h"
#include "main_task.h"
#include "coro_utility.h"



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


int main() {
    // co_main().RunLoop<TimedEventLoop, IOUringEventLoop>();
    std::cout << "123.123.123.123"_addr.s_addr << std::endl;
}

