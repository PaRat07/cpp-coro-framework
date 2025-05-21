<<<<<<< Updated upstream
=======
#include "epoll_event_loop.h"
using namespace epoll;

>>>>>>> Stashed changes
#include <http.h>
#include <chrono>
#include "io_uring_event_loop.h"

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <rfl.hpp>
#include <rfl/json.hpp>
#include <sys/socket.h>
#include <thread>
#include "coro_utility.h"
#include "errno.h"
#include "main_task.h"
#include "task.h"
#include "timed_event_loop.h"
#include <pqxx/pqxx>

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace std::string_view_literals;

struct InvokeOnConstruct {
    InvokeOnConstruct(auto &&f) {
        f();
    }
};

#define CONCAT_IMPL(a, b) a##b
#define CONCAT(a, b) CONCAT_IMPL(a, b)
#define ONCE static InvokeOnConstruct CONCAT(unique_name, __LINE__) = [&]

<<<<<<< Updated upstream
auto Loop(int fd/*, pqxx::connection &db_conn*/) -> Task<> {
=======
auto ProcConn(File connfd/*, pqxx::connection &db_conn*/) -> Task<> {
>>>>>>> Stashed changes
    // ONCE {
    //     db_conn.prepare("get_by_id", R"("SELECT "id", "randomnumber" FROM "world" WHERE id = $1")");
    // };
    std::cout << "Started" << std::endl;
    std::array<char, 1024> resp_buf;
    HttpParser<1024> parser(-1);
    // pqxx::nontransaction tx(db_conn);
    while (true) {
        int connfd = co_await AcceptIPV4(fd);
        bool reuse_connection = true;
        parser.Reconnect(connfd);
        try {
            while (reuse_connection) {
                HttpRequest req = co_await parser.ParseRequest();
                reuse_connection = req.keep_alive;
                if (req.request_target == "/plaintext") {
                    co_await SendResponse(connfd, resp_buf, {
                        {
                            { "Content-Type", "text/plain; charset=UTF-8" },
                            { "Server", "Example" },
                            { "Connection", "keep-alive" }
                        },
                        "Hello, world!"
                    });
                } else if (req.request_target == "/json") {
                    struct JsonResp {
                        std::string_view message;
                    };
                    std::string body = rfl::json::write(JsonResp{ .message = "Hello, World!" });
                    co_await SendResponse(connfd, resp_buf, {
                        {
                            { "Content-Type", "application/json; charset=UTF-8" },
                            { "Server", "Example" },
                            { "Connection", "keep-alive" }
                        },
                        std::move(body)
                    });
                } else if (req.request_target == "/db") {
                    int random_id = rand() % 10'000;
                    struct DbResp {
                        int id;
                        int randomNumber;
                    };
                    DbResp resp;
                    // for (auto [resp_id, resp_num] : tx.query<int, int>(pqxx::prepped("get_by_id"), random_id)) {
                    //     resp = { resp_id, resp_num };
                    // }
                    std::string body = rfl::json::write(resp);
                    co_await SendResponse(connfd, resp_buf, {
                        {
                            { "Content-Type", "application/json; charset=UTF-8" },
                            { "Server", "Example" },
                            { "Connection", "keep-alive" }
                        },
                        std::move(body)
                    });
                } else {
                    throw std::runtime_error("incorrect prefix");
                }
            }
        } catch (const std::exception &exc) {
            close(connfd);
            std::cerr << "Failed: " << std::quoted(exc.what()) << std::endl;
        }
<<<<<<< Updated upstream
=======
    } catch (...) {
        std::cerr << "Failed: " << std::endl;
>>>>>>> Stashed changes
    }
}


<<<<<<< Updated upstream
MainTask co_server(int fd) {
    std::array<Task<>, 3'000 * 0 + 10> tasks;
    // std::string db_options = fmt::format("host=localhost port=5432 dbname=hello_world connect_timeout=10 password={} user={}", std::getenv("PGPASS"), std::getenv("PGUSER"));
    // pqxx::connection db_conn;//(db_options.data());
    for (int i = 0; i < tasks.size(); ++i) {
        tasks[i] = Loop(fd/*, db_conn*/);
=======
MainTask co_server(File fd) {
    while (true) {
      spawn(ProcConn(co_await fd.Accept()));
>>>>>>> Stashed changes
    }
    co_await WhenAll(tasks);
    co_return;
}

int main() {
    signal(SIGPIPE, SIG_IGN);
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);

    int one = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0) {
        throw std::system_error(errno, std::system_category(), "setsockopt SO_REUSEADDR");
    }
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one)) < 0) {
        throw std::system_error(errno, std::system_category(), "setsockopt SO_REUSEPORT");
    }
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));


    sockaddr_in addr;
    addr.sin_family = AF_INET;          // IPv4
    addr.sin_addr.s_addr = INADDR_ANY;  // 0.0.0.0
    addr.sin_port = htons(8080);        // Port 8888
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        throw std::system_error(errno, std::system_category(), "bind error");
    }

<<<<<<< Updated upstream
    if (listen(fd, SOCK_STREAM) < 0) {
        close(fd);
        throw std::system_error(errno, std::system_category(), "listen error");
    }
    fork();fork();fork();fork();
    co_server(fd).RunLoop<IOUringEventLoop>();
=======
    if (listen(fd, std::numeric_limits<int>::max()) < 0) {
        close(fd);
        throw std::system_error(errno, std::system_category(), "listen error");
    }
    // fork();fork();fork();fork();

  // while (true) {
  //   int connfd;
  //   [&connfd, fd] mutable -> MainTask {
  //     connfd = co_await AcceptIPV4(fd);
  //   } ().RunLoop<IOUringEventLoop>();
  //
  //   [connfd] -> MainTask {
  //     co_await ProcConn(connfd);
  //   } ().RunLoop<IOUringEventLoop>();
  // }

    co_server(fd).RunLoop<EpollEventLoop>();
>>>>>>> Stashed changes
}



// wrk -H 'Host: tfb-server' -H 'Accept: text/plain,text/html;q=0.9,application/xhtml+xml;q=0.9,application/xml;q=0.8,*/*;q=0.7' -H 'Connection: keep-alive' --latency -d 15 -c 16384 --timeout 8 -t 16 http://localhost:8080/plaintext -s pipeline.lua -- 16
// wrk -H 'Host: tfb-server' -H 'Accept: text/plain,text/html;q=0.9,application/xhtml+xml;q=0.9,application/xml;q=0.8,*/*;q=0.7' -H 'Connection: keep-alive' --latency -d 15 -c 1024  --timeout 8 -t 1  http://localhost:8080/plaintext -s pipeline.lua -- 16
