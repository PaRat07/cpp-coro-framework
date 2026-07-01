#include <../include/runtime/sync/coro_utility.h>

#include <iostream>

Task<> WhenAll(std::span<Task<>> tasks) {
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

void _log_spawn_exception(const std::exception* ep) {
  if (ep == nullptr) {
      std::cerr << "Thrown out of spawned coro: <unknow exception type>" << std::endl;
  } else {
      std::cerr << "Thrown out of spawned coro: " << ep->what() << std::endl;
  }
}

