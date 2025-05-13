#pragma once

#include <coroutine>

#include "task.h"

// usage std::coroutine_handle<> my_handle = co_await Self();
inline auto Self() {
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
using DevoidifyedT = std::conditional_t<std::is_same_v<T, void>, std::type_identity<void>, T>;

template<typename T>
Task<DevoidifyedT<T>> Devoidify([[clang::coro_await_elidable_argument]] Task<T>&& task) {
    if constexpr (std::is_same_v<T, void>) {
        co_await task;
        co_return {};
    } else {
        co_return co_await task;
    }
}

template<typename T>
Task<> SaveTo([[clang::coro_await_elidable_argument]] Task<T> &&task, T &place) {
    place = co_await task;
    co_return;
}

template<std::same_as<Task<>>... Ts>
Task<> WhenAllImpl([[clang::coro_await_elidable_argument]] Ts&&... tasks) {
    std::coroutine_handle<> self = co_await Self();
    ((tasks.GetHandle().promise().caller_handle = self), ...);
    (tasks.GetHandle().resume(), ...);
    for (size_t i = 0; i < sizeof...(Ts); ++i) {
        co_await std::suspend_always{};
    }
    co_return;
}


// waits for all the coros and returns tuple of their results
// if task returns void it returns std::type_identity<void>
template<typename... Ts>
Task<std::tuple<DevoidifyedT<Ts>...>> WhenAll([[clang::coro_await_elidable_argument]] Task<Ts>&&... tasks) {
    std::tuple<DevoidifyedT<Ts>...> ans;
    co_await [&ans] <size_t... Inds> (std::index_sequence<Inds...>, [[clang::coro_await_elidable_argument]] Task<Ts>&&... tasks) -> Task<> {
        co_await WhenAllImpl(SaveTo(Devoidify(std::move(tasks...[Inds])), std::get<Inds>(ans))...);
    } (std::make_index_sequence<sizeof...(Ts)>{}, std::move(tasks)...);

    co_return ans;
}

Task<> WhenAll([[clang::coro_await_elidable_argument]] std::span<Task<>> tasks) {
    std::coroutine_handle<> self = co_await Self();
    for (auto &i : tasks) {
        i.GetHandle().promise().caller_handle = self;
    }
    for (auto &i : tasks) {
        i.GetHandle().resume();
    }
    for (size_t i = 0; i < tasks.size(); ++i) {
        co_await std::suspend_always{};
    }
    co_return;
}
