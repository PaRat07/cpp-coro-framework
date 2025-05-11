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
            if (n > main_task_aloc_buf.size()) [[unlikely]] {
                std::println(std::cerr, "Unable to allocate MainTask on stack because it's size is greater than buffer's one({} > {})", n, main_task_aloc_buf.size());
                return ::operator new(n);
            } else if (used_main_task_buf) [[unlikely]] {
                std::println(std::cerr, "Unable to allocate MainTask on stack because it's allocated more than one time (you must use MainTask only for co_main)");
                return ::operator new(n);
            }
            used_main_task_buf = true;
            return main_task_aloc_buf.data();
        }

        void operator delete(void *data) {
            if (data != main_task_aloc_buf.data()) [[unlikely]] {
                std::println(std::cerr, "Deallocating MainTask allocated on heap");
                ::operator delete(data);
            }
        }

        void return_void() noexcept {}

        std::suspend_never initial_suspend() noexcept { return {}; }

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
    void RunLoop() {
        while ((!EvLoops::IsEmpty() || ...)) {
            ((EvLoops::Resume()), ...);
        }
        if (handle_.promise().exc_ptr) {
            std::rethrow_exception(handle_.promise().exc_ptr);
        }
    }

private:
    explicit MainTask(std::coroutine_handle<Promise> handle) noexcept
        : handle_(handle)
    {
    }

    std::coroutine_handle<Promise> handle_;
};
