#include "io_uring_event_loop.h"
using namespace uring;
#include "apq.h"
#include <chrono>
#include <http.h>

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
#include <sys/wait.h>
using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace std::string_view_literals;

struct InvokeOnConstruct {
    InvokeOnConstruct(auto&& f) { f(); }
};

#define CONCAT_IMPL(a, b) a##b
#define CONCAT(a, b) CONCAT_IMPL(a, b)
#define ONCE static InvokeOnConstruct CONCAT(unique_name, __LINE__) = [&]

auto ProcConn(File connfd) -> Task<> {
    static auto sttmnt = co_await PostgresEventLoop::Prepare<int>(R"(SELECT * FROM "people" WHERE id = $1;)");
    std::array<char, 1024> resp_buf;
    HttpParser<1024> parser(connfd);
    bool reuse_connection = true;
    try {
        while (reuse_connection) {
            HttpRequest req = co_await parser.ParseRequest();
            reuse_connection = req.keep_alive;
            if (req.request_target == "/plaintext") {
                co_await SendResponse(connfd, resp_buf,
                                      std::array{
                                        std::pair{"Content-Type"sv, "text/plain; charset=UTF-8"sv},
                                        std::pair{"Server"sv, "Example"sv},
                                        std::pair{"Connection"sv, "keep-alive"sv}
                                      },
                                       "Hello, world!");
            }
            else if (req.request_target == "/json") {
                struct JsonResp {
                    std::string_view message;
                };
                std::string body = rfl::json::write(JsonResp{.message = "Hello, World!"});
                co_await SendResponse(connfd, resp_buf,
                                      std::array{
                                        std::pair{"Content-Type"sv, "application/json; charset=UTF-8"sv},
                                        std::pair{"Server"sv, "Example"sv},
                                        std::pair{"Connection"sv, "keep-alive"sv}
                                      },
                                       body);
            }
            else if (req.request_target == "/db") {
                int random_id = rand() % 10'000;
                struct DbResp {
                    int id;
                    int randomNumber;
                };
                DbResp resp;
                for (auto [ resp_id, resp_num] : co_await SendPQReq<std::tuple<int, int>>(sttmnt, std::byteswap(random_id))) {
                    resp = { std::byteswap(resp_id), std::byteswap(resp_num) };
                }
                std::string body = rfl::json::write(resp);
                co_await SendResponse(connfd, resp_buf,
                                      std::array{
                                        std::pair{"Content-Type"sv, "application/json; charset=UTF-8"sv},
                                        std::pair{"Server"sv, "Example"sv},
                                        std::pair{"Connection"sv, "keep-alive"sv}
                                      },
                                       body);
            }
            else {
                throw std::runtime_error("incorrect prefix");
            }
        }
    } catch (...) {
      // fmt::println("failed");
        // std::cerr << "Failed: " << std::endl;
    }
    co_return;
}

MainTask co_server(File fd) {
      while (true) {
        spawn(ProcConn(co_await fd.Accept()));
      }
      co_return;
}


void fork_workers() {
    int worker_count = 0;
    pid_t pid;
    cpu_set_t online_cpus, cpu;

    signal(SIGPIPE, SIG_IGN);

    // Get set/count of all online CPUs
    CPU_ZERO(&online_cpus);
    sched_getaffinity(0, sizeof(online_cpus), &online_cpus);
    int num_online_cpus = CPU_COUNT(&online_cpus);

    // Create a mapping between the relative cpu id and absolute cpu id for cases where the cpu ids are not contiguous
    // E.g if only cpus 0, 1, 8, and 9 are visible to the app because taskset was used or because some cpus are offline
    // then the mapping is 0 -> 0, 1 -> 1, 2 -> 8, 3 -> 9
    int rel_to_abs_cpu[num_online_cpus];
    int rel_cpu_index = 0;

    for (int abs_cpu_index = 0; abs_cpu_index < CPU_SETSIZE; abs_cpu_index++) {
        if (CPU_ISSET(abs_cpu_index, &online_cpus)) {
            rel_to_abs_cpu[rel_cpu_index] = abs_cpu_index;
            rel_cpu_index++;

            if (rel_cpu_index == num_online_cpus)
                break;
        }
    }

    for (int i = 0; i < num_online_cpus; i++) {
        pid = Unwrap(fork());
        if (pid > 0) {

            worker_count++;
            fprintf(stderr, "Worker running on CPU %d\n", i);
            continue;
        }
        if (pid == 0) {
            CPU_ZERO(&cpu);
            CPU_SET(rel_to_abs_cpu[i], &cpu);
            Unwrap(sched_setaffinity(0, sizeof cpu, &cpu));
            return;
        }
    }

    (void)fprintf(stderr, "libreactor running with %d worker processes\n", worker_count);

    wait(NULL); // wait for children to exit
    (void)fprintf(stderr, "A worker process has exited unexpectedly. Shutting down.\n");
    exit(EXIT_FAILURE);
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
    // struct sock_filter code[] = {{BPF_LD | BPF_W | BPF_ABS, 0, 0, (__u32)SKF_AD_OFF + SKF_AD_CPU}, {BPF_RET | BPF_A, 0, 0, 0}};
    // struct sock_fprog prog = { .len = sizeof(code)/sizeof(code[0]), .filter = code };
    // setsockopt(fd, SOL_SOCKET, SO_ATTACH_REUSEPORT_CBPF, &prog, sizeof(prog));


    sockaddr_in addr;
    addr.sin_family = AF_INET; // IPv4
    addr.sin_addr.s_addr = INADDR_ANY; // 0.0.0.0
    addr.sin_port = htons(8080); // Port 8888
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        throw std::system_error(errno, std::system_category(), "bind error");
    }

    if (listen(fd, std::numeric_limits<int>::max()) < 0) {
        close(fd);
        throw std::system_error(errno, std::system_category(), "listen error");
    }
    // fork_workers();
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

    co_server(fd).RunLoop<IOUringEventLoop, PostgresEventLoop>();
}


// wrk -H 'Host: tfb-server' -H 'Accept: text/plain,text/html;q=0.9,application/xhtml+xml;q=0.9,application/xml;q=0.8,*/*;q=0.7' -H 'Connection: keep-alive' --latency -d 15 -c 16384 --timeout 8 -t 16 http://localhost:8080/plaintext -s pipeline.lua -- 16
// wrk -H 'Host: tfb-server' -H 'Accept: text/plain,text/html;q=0.9,application/xhtml+xml;q=0.9,application/xml;q=0.8,*/*;q=0.7' -H 'Connection: keep-alive' --latency -d 15 -c 1024  --timeout 8 -t 1  http://localhost:8080/plaintext -s pipeline.lua -- 16
