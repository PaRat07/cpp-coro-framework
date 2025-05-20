#include <http.h>
#include <chrono>
#include "epoll_event_loop.h"

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

template<template<typename> bool T>
void f(){}

template<typename>
using T = int;

int main() {
    f<T>();
    co_main().RunLoop<TimedEventLoop, IOUringEventLoop>();
}
