#pragma once

#include <array>
#include <exception>
#include <coroutine>
#include <print>
#include <iostream>

alignas(max_align_t) inline std::array<std::byte, 10'000> main_task_aloc_buf;
static bool used_main_task_buf = false;
// task for co_main
class [[CORO_ATTRIBUTES]] MainTask {
public:
    struct Promise {
        std::exception_ptr exc_ptr;

        MainTask get_return_object() noexcept {
            return MainTask { std::coroutine_handle<Promise>::from_promise(*this) };
        }

        void unhandled_exception() noexcept {
            exc_ptr = std::current_exception();
        }

        void* operator new(std::size_t n) {
            std::println("Allocated: {}", n);
            return ::operator new(n);
        }

        void return_void() noexcept {}

        std::suspend_always initial_suspend() noexcept { return {}; }

        std::suspend_always final_suspend() noexcept { return {}; }
    };

    using promise_type = Promise;

    MainTask() = default;

    ~MainTask() {
        handle_.destroy();
    }

    MainTask(MainTask&& other) = delete;
    MainTask(const MainTask& other) = delete;


    template<typename... EvLoops>
    [[noreturn]] void RunLoop() {
        (EvLoops::Init(), ...);
        handle_.resume();
        while (true) {
            ((EvLoops::Resume()), ...);
        }
    }

private:
    explicit MainTask(std::coroutine_handle<Promise> handle) noexcept
        : handle_(handle)
    {
    }

    std::coroutine_handle<Promise> handle_;
};
