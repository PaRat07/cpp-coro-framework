#pragma once

#include <coroutine>

#include "task.h"

// usage std::coroutine_handle<> my_handle = co_await Self();
auto Self() {
    struct SelfAwaitable {
        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> handle) noexcept {
            handle_ = handle;
            handle.resume();
        }

        std::coroutine_handle<> await_resume() const noexcept {
            return handle_;
        }
        std::coroutine_handle<> handle_;
    };

    return SelfAwaitable{};
}

// waits for all the coros and returns tuple of their results
// TODO: what if coroutine returns void
template<typename... Ts>
Task<std::tuple<Ts...>> WhenAll([[clang::coro_await_elidable_argument]] Task<Ts>&&... tasks) {
    co_return std::tuple { co_await tasks... };
}
