#pragma once

#include <source_location>
#include <system_error>
#include <format>

int Unwrap(int res,
            std::source_location loc = std::source_location::current()) {
  if (res < 0) [[unlikely]] {
    throw std::system_error(errno, std::generic_category(),
                            std::format(R"("{}:{}: {}: Unwrap failed)", loc.file_name(), loc.line(), loc.function_name()));
  }
  return res;
}