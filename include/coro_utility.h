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

template<typename T>
Task<std::conditional_t<std::is_same_v<T, void>, std::type_identity<void>, T>> Devoidify([[clang::coro_await_elidable_argument]] Task<T>&& task) {
    if constexpr (std::is_same_v<T, void>) {
        co_await task;
        co_return {};
    } else {
        co_return co_await task;
    }
}


// waits for all the coros and returns tuple of their results
// if task returns void it returns std::type_identity<void>
template<typename... Ts>
Task<std::tuple<Ts...>> WhenAll([[clang::coro_await_elidable_argument]] Task<Ts>&&... tasks) {
    co_return std::tuple { co_await Devoidify(std::move(tasks))... };
}
