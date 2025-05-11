#include <chrono>

#include "task.h"
#include "timed_event_loop.h"
#include "main_task.h"
#include "coro_utility.h"



// USER CODE
using namespace std::chrono_literals;

Task<int> Get(int x) noexcept {
    std::println("Before: {}", x);
    co_await SleepFor(1s);
    std::println("After: {}", x);
    co_return x;
}


Task<int> Print() noexcept {
    std::println("Got: {}", co_await Get(13));
    co_return 1;
}

MainTask co_main() {
    std::apply([] (auto... args) {
        ((std::cout << args << std::endl), ...);
    }, co_await WhenAll(Print(), Print()));
    co_return;
}

int main() {
    co_main().RunLoop<TimedEventLoop>();
}

