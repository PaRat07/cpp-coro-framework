#pragma once

// #include "io_uring_event_loop.h"

#include <memory>
// using namespace uring;

#include <algorithm>
#include <chrono>
#include <coroutine>
#include <queue>
#include <ranges>
#include <span>
#include <string>

#include "coro_utility.h"
#include "fmt/format.h"
#include "my-queue.h"
#include "postgresql/libpq-fe.h"
#include "sys_utility.h"
#include "task.h"
#include "timed_event_loop.h"
namespace internal {
void Unwrap(PGconn *conn, PGresult *res) {
  if (PQresultStatus(res) != PGRES_COMMAND_OK) [[unlikely]] {
    throw std::runtime_error(fmt::format("Unwrap failed: {}", PQerrorMessage(conn)));
  }
}

void Unwrap(PGconn *conn, bool res) {
  if (!res) [[unlikely]] {
    throw std::runtime_error(fmt::format("Unwrap failed: {}", PQerrorMessage(conn)));
  }
}
} // namespace internal

struct Connection {
public:
  Connection(std::string init_state) {
    conn = PQconnectdb(init_state.data());
    internal::Unwrap(conn, PQstatus(conn) == CONNECTION_OK);
    internal::Unwrap(conn, 0 == PQsetnonblocking(conn, 1));
  }

  PGconn *GetRaw() {
    return conn;
  }

private:
  PGconn *conn;
};


using PGresPtr = std::unique_ptr<PGresult, decltype([] (PGresult *ptr) { PQclear(ptr); })>;

template<typename... Ts>
struct StmtntString {
  template<size_t kSz>
  consteval StmtntString(const char (&data_chrptr)[kSz]) : data(data_chrptr) {
    std::variant<int, std::monostate> var(std::in_place_type<std::monostate>);
    int max_num = 0;
    for (char i : data) {
      if (i == '$') {
        var.emplace<int>(0);
      } else if (i >= '0' && i <= '9') {
        var.visit(overloaded {
          [] (std::monostate) {},
          [i] (int &num) { num = num * 10 + (i - '0'); }
        });
      } else {
        var.visit(overloaded {
          [] (std::monostate) {},
          [&max_num, &var] (int num) { max_num = std::max(max_num, num); var.emplace<std::monostate>(); }
        });
      }
    }
    var.visit(overloaded {
      [] (std::monostate) {},
      [&max_num] (int num) { max_num = std::max(max_num, num); }
    });
    if (max_num != sizeof...(Ts)) {
      // CE
      throw std::invalid_argument("invalid statement string for this template parameters");
    }
  }

  std::string_view data;
};


namespace internal {
// fuck me https://github.com/userver-framework/userver/blob/208a820640ecef02199521932239e90751122b48/postgresql/src/storages/postgres/exceptions.cpp#L18
template<typename T>
struct OidVal;

template<>
struct OidVal<int32_t> : std::integral_constant<Oid, 23> {};

template<>
struct OidVal<std::string> : std::integral_constant<Oid, 25> {};

template<>
struct OidVal<std::string_view> : std::integral_constant<Oid, 25> {};


static size_t sttmnt_cnt = 0;
} // namespace internal


template<typename... Ts>
class PreparedStmnt {
public:
  PreparedStmnt(Connection &conn, StmtntString<Ts...> stmnt) : name_(fmt::format("unique_sttmnt_name{}", internal::sttmnt_cnt++)) {
    static constexpr std::array<Oid, sizeof...(Ts)> types = { internal::OidVal<Ts>::value... };
    internal::Unwrap(conn.GetRaw(), 1 == PQsendPrepare(conn.GetRaw(), name_.data(), stmnt.data.data(), sizeof...(Ts), types.data()));
    {
      PGresPtr resp{PQgetResult(conn.GetRaw())};
      assert(PQresultStatus(resp.get()) == PGRES_COMMAND_OK);
    }
    assert(!PQgetResult(conn.GetRaw()));
  }

  std::string_view GetName() const {
    return name_;
  }

private:
  std::string name_;
};


namespace internal {
// numbers must be in big endian because idgaf
template<typename... Ts>
void Execute(Connection &conn, const PreparedStmnt<Ts...> &stmnt, const Ts&... args) {
  static constexpr std::array<Oid, sizeof...(Ts)> types = { internal::OidVal<Ts>::value... };
  static constexpr std::array<int, sizeof...(Ts)> szs = {
    [&] {
      using ArgT = std::remove_cvref_t<decltype(args)>;
      if constexpr (!std::is_integral_v<ArgT>) {
        return args.size();
      } else {
        return sizeof(ArgT);
      }
    } ()...
  };
  static constexpr std::array<int, sizeof...(Ts)> format = { std::is_integral_v<std::remove_cvref_t<decltype(args)>>... };

  std::array<const char*, sizeof...(Ts)> args_ptrs;
  if constexpr (sizeof...(args) > 0) {
    [&args_ptrs] <size_t Ind> (this auto self, std::integral_constant<size_t, Ind>, const auto &sep_arg, const auto&... args) {
      if constexpr (std::is_integral_v<std::remove_cvref_t<decltype(sep_arg)>>) {
        args_ptrs[Ind] = reinterpret_cast<const char*>(&sep_arg);
      } else {
        args_ptrs[Ind] = sep_arg.data();
      }
      // args_ptrs[Ind] = overloaded {
      //    [] (std::integral auto &&sep_arg) { return reinterpret_cast<const char*>(&sep_arg); },
      //    [] (              auto &&sep_arg) { return sep_arg.data(); }
      // } (sep_arg);
      if constexpr (Ind + 1 < sizeof...(Ts)) {
        self(std::integral_constant<size_t, Ind + 1>{}, args...);
      }
    } (std::integral_constant<size_t, 0>{}, args...);
  }
  internal::Unwrap(conn.GetRaw(), PQsendQueryPrepared(conn.GetRaw(), stmnt.GetName().data(), types.size(), args_ptrs.data(), szs.data(), format.data(), 1));
}

template<typename T>
T ParseRow(ptrdiff_t row_ind, PGresult *res) {
  return overloaded {
    [=] (std::type_identity<int32_t>) {
      assert(PQnfields(res) == 1);
      int32_t ans;
      std::memcpy(&ans, PQgetvalue(res, row_ind, 0), sizeof(ans));
      return ans;
    },
    [=] (std::type_identity<std::string>) {
      assert(PQnfields(res) == 1);
      return std::string(PQgetvalue(res, row_ind, 0));
    },
    [=] <typename... Ts> (std::type_identity<std::tuple<Ts...>>) -> T {
      assert(PQnfields(res) == sizeof...(Ts));
      T ans;
      [res, row_ind, &ans] <size_t... Inds> (std::index_sequence<Inds...>) {
        ([res, row_ind, &ans] <size_t Ind> (std::index_sequence<Ind>) {
          std::get<Ind>(ans) = overloaded {
            [res, row_ind] (std::type_identity<int32_t>) {
              int32_t ans;
              std::memcpy(&ans, PQgetvalue(res, row_ind, Ind), sizeof(ans));
              return ans;
            },
            [res, row_ind] (std::type_identity<std::string>) {
              return std::string(PQgetvalue(res, row_ind, Ind));
            }
          } (std::type_identity<std::tuple_element_t<Ind, T>>{});
        } (std::index_sequence<Inds>{}), ...);
      } (std::make_index_sequence<std::tuple_size_v<T>>{});
      return ans;
    }
  } (std::type_identity<T>{});
}


template<typename T>
static std::vector<T> Parse(PGresult *res) {
  if (res == nullptr) return {};
  std::vector<T> ans(PQntuples(res));
  for (ptrdiff_t ind = 0; ind < ans.size(); ++ind) {
    ans[ind] = ParseRow<T>(ind, res);
  }
  return ans;
}

template<typename T>
Task<std::vector<T>> Recieve(Connection &conn) {
  std::vector<T> ans;
  while (true) {
    if (PQisBusy(conn.GetRaw())) {
      co_await File(PQsocket(conn.GetRaw())).Poll(true);
      PQconsumeInput(conn.GetRaw());
    } else {
      break;
    }
  }
  while (auto res_ptr = PGresPtr(PQgetResult(conn.GetRaw()))) {
    switch (PQresultStatus(res_ptr.get())) {
    case PGRES_TUPLES_OK: {
      std::ranges::copy(Parse<T>(res_ptr.get()), std::back_inserter(ans));
      break;
    }
    case PGRES_FATAL_ERROR: {
      Unwrap(conn.GetRaw(), false);
      break;
    }
    default: {
      // idk
    }
    }
  }
  co_return ans;
}
} // namespace internal

template<typename T, typename... Ts>
Task<std::vector<T>> Exec(Connection &conn, PreparedStmnt<Ts...> &stmnt, const Ts&... args) {
  co_await File(PQsocket(conn.GetRaw())).Poll(false);
  internal::Execute(conn, stmnt, args...);
  co_return co_await internal::Recieve<T>(conn);
}
