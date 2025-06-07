#pragma once

#include "io_uring_event_loop.h"

#include <memory>
using namespace uring;

#include <algorithm>
#include <chrono>
#include <coroutine>
#include <queue>
#include <ranges>
#include <span>
#include <string>

#include "fmt/format.h"
#include "coro_utility.h"
#include "my-queue.h"
#include "postgresql/libpq-fe.h"
#include "sys_utility.h"
#include "task.h"

void Unwrap(PGconn *conn, PGresult *res) {
  if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    throw std::runtime_error(fmt::format("Unwrap failed: {}", PQerrorMessage(conn)));
  }
}
struct Connection {
  Connection(std::string_view conn_str) {
    conn = PQconnectdb(conn_str.data());
  }

  PGconn *conn;
};

// template<size_t Sz>
// struct ConstexprString {
//   consteval ConstexprString(char (&data_val)[Sz]) {
//     std::ranges::copy(data_val, data);;
//   }
//
//
//   char data[Sz];
// };

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
template<typename... Ts>
class PreparedStmnt {
public:
  PreparedStmnt(Connection &conn, StmtntString<Ts...> stmnt) : name_(fmt::format("unique_sttmnt_name{}", sttmnt_cnt++)) {
    static constexpr std::array<Oid, sizeof...(Ts)> types = { OidVal<Ts>::value... };
    auto res = PQprepare(conn.conn, name_.data(), stmnt.data.data(), sizeof...(Ts), types.data());
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
      fmt::println("prep err: {}", PQerrorMessage(conn.conn));
    }
  }

  std::string_view GetName() const {
    return name_;
  }

private:
  std::string name_;
};

class Pipelined {
public:
  Pipelined(Connection &conn) {
    conn_ = conn.conn;
    PQenterPipelineMode(conn_);
  }

  ~Pipelined() {
    PQexitPipelineMode(conn_);
  }

  // numbers must be in big endian because idgaf
  template<typename... Ts>
  void Execute(PreparedStmnt<Ts...> stmnt, const Ts&... args) {
    // correct
    static constexpr std::array<Oid, sizeof...(Ts)> types = { OidVal<Ts>::value... };
    // correct
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
    // correct
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
    Unwrap(PQsendQueryPrepared(conn_, stmnt.GetName().data(), types.size(), args_ptrs.data(), szs.data(), format.data(), 1));
    Unwrap(PQpipelineSync(conn_));
    Unwrap(PQflush(conn_));
  }

  template<typename T>
  std::vector<T> Recieve() {
    auto res = PQgetResult(conn_);
    while (res == nullptr) {
      res = PQgetResult(conn_);
    }
    std::vector<T> ans(PQntuples(res));
    for (ptrdiff_t ind = 0; ind < ans.size(); ++ind) {
      ans[ind] = ParseRow<T>(ind, res);
    }
    PQclear(res);
    return ans;
  }
private:

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

  PGconn *conn_;
};


struct PipelinedConnection {
  void Init() {
    conn = PQconnectdb(init_state.data());
    if (PQstatus(conn) != CONNECTION_OK) {
      PQfinish(conn);
      throw std::runtime_error("failed to connect to db");
    }
  }
  std::string init_state;


// private:
    PGconn *conn;
};

// class PostgresEventLoop {
// public:
//   struct ResHolder {
//     pqxx::result res;
//     std::coroutine_handle<> handle;
//   };
//
//   static void Resume() {
//
//   }
//
//   static void Init() {
//     conn.Init();
//
//     spawn([] -> Task<> {
//       File fd{conn.conn.sock()};
//       while (true) {
//         co_await fd.Poll(true);
//         ResHolder *res_ptr = to_resume.Pop();
//         res_ptr->res = conn.GetPipe().retrieve().second;
//         res_ptr->handle.resume();
//       }
//       co_return;
//     });
//   }
//
//   struct ReqAwaitable {
//     bool await_ready() const noexcept { return false; }
//
//     void await_suspend(std::coroutine_handle<> handle) noexcept {
//       conn.GetPipe().insert(req);
//       res.handle = handle;
//       to_resume.Push(&res);
//     }
//
//     pqxx::result await_resume() const noexcept { return res.res; }
//
//     std::string_view req;
//     ResHolder res;
//   };
//
// private:
//   static inline PipelinedConnection conn;
//   static inline Queue<ResHolder*> to_resume;
// };
//
// inline Task<pqxx::result> SendPQReq(std::string_view sv) {
//   co_return co_await PostgresEventLoop::ReqAwaitable {
//     .req = sv
//   };
// }
