#include <cstring>
#include <iterator>
#include <rpcws.hpp>
#include <sys/ioctl.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <termios.h>
#include <unistd.h>

#include "utils.hpp"

LOAD_ENV(NSGOD_API, "ws+unix://nsgod.socket");

union ptr_union {
  char *buffer;
  uint64_t _;
};

static_assert(sizeof(ptr_union) == 8);

void printHelp();

enum struct Mode {
  unknown,
  print_help,
  print_version,
  all_log,
  all_status,
  start,
  status,
  stop,
  log,
  kill,
  erase,
  send,
  wait,
  shutdown,
  attach,
};

int main(int argc, char **argv) {
  using namespace rpcws;

  static Mode mode;
  static std::string strbuf;
  static json body;

  if (argc == 1) {
    mode = Mode::print_help;
  } else if (argc == 2) {
    if (strcmp(argv[1], "status") == 0)
      mode = Mode::all_status;
    else if (strcmp(argv[1], "version") == 0)
      mode = Mode::print_version;
    else if (strcmp(argv[1], "log") == 0)
      mode = Mode::all_log;
    else if (strcmp(argv[1], "help") == 0)
      mode = Mode::print_help;
    else if (strcmp(argv[1], "shutdown") == 0)
      mode = Mode::shutdown;
  } else if (argc == 3) {
    if (strcmp(argv[1], "status") == 0)
      mode = Mode::status;
    else if (strcmp(argv[1], "stop") == 0)
      mode = Mode::stop;
    else if (strcmp(argv[1], "start") == 0)
      mode = Mode::start;
    else if (strcmp(argv[1], "log") == 0)
      mode = Mode::log;
    else if (strcmp(argv[1], "erase") == 0)
      mode = Mode::erase;
    else if (strcmp(argv[1], "send") == 0)
      mode = Mode::send;
    else if (strcmp(argv[1], "wait") == 0)
      mode = Mode::wait;
    else if (strcmp(argv[1], "attach") == 0)
      mode = Mode::attach;
  } else if (argc == 4) {
    if (strcmp(argv[1], "kill") == 0) mode = Mode::kill;
  }

  switch (mode) {
  case Mode::print_help: printHelp(); return EXIT_SUCCESS;
  case Mode::unknown: std::cerr << "Unknown subcommand " << argv[1] << std::endl; return EXIT_FAILURE;
  case Mode::send: {
    std::cin >> std::noskipws;
    std::istream_iterator<char> it{ std::cin };
    std::istream_iterator<char> end;
    strbuf = { it, end };
  } break;
  case Mode::start: {
    std::cin >> std::noskipws;
    std::istream_iterator<char> it{ std::cin };
    std::istream_iterator<char> end;
    std::string results{ it, end };
    body = json::parse(results);
  } break;
  default: break;
  }

  auto ep = std::make_shared<epoll>();
  static RPC::Client instance{ std::make_unique<client_wsio>(NSGOD_API, ep) };
  static auto &handler = *ep;
  static auto do_print = [](auto data) { std::cout << data << std::endl; };
  static auto do_close = [](auto) {
    instance.stop();
    handler.shutdown();
  };
  static auto do_fail = [](auto ex) {
    try {
      if (ex) std::rethrow_exception(ex);
    } catch (RemoteException const &ex) { std::cout << ex.full << std::endl; } catch (std::exception const &ex) {
      std::cout << ex.what() << std::endl;
    }
    instance.stop();
    handler.shutdown();
  };

  instance.start()
      .then<promise<json>>([] { return instance.call("ping", json::object({})); })
      .then([=](auto) {
        switch (mode) {
        case Mode::shutdown: {
          instance.call("shutdown", json::object({})).then(do_close).fail(do_fail);
        } break;
        case Mode::print_version: {
          instance.call("version", json::object({})).then(do_print).then(do_close);
        } break;
        case Mode::all_log: {
          instance
              .on("output",
                  [](json data) {
                    auto tag = "[" + data["service"].get<std::string>() + "]";
                    std::istringstream iss{ data["data"].get<std::string>() };
                    std::string line;
                    while (std::getline(iss, line)) std::cout << tag << line << std::endl;
                  })
              .fail(do_fail);
        } break;
        case Mode::all_status: {
          instance.call("status", json::object({})).then(do_print).then(do_close).fail(do_fail);
        } break;
        case Mode::status: {
          instance.call("status", json::object({ { "service", argv[2] } })).then(do_print).then(do_close).fail(do_fail);
        } break;
        case Mode::stop: {
          instance.call("kill", json::object({ { "service", argv[2] }, { "signal", SIGTERM } })).then(do_close).fail(do_fail);
        } break;
        case Mode::kill: {
          instance.call("kill", json::object({ { "service", argv[2] }, { "signal", atoi(argv[3]) } })).then(do_close).fail(do_fail);
        } break;
        case Mode::erase: {
          instance.call("erase", json::object({ { "service", argv[2] } })).then(do_print).then(do_close).fail(do_fail);
        } break;
        case Mode::send: {
          instance.call("send", json::object({ { "service", argv[2] }, { "data", strbuf } })).then(do_print).then(do_close).fail(do_fail);
        } break;
        case Mode::start: {
          instance.call("start", json::object({ { "service", argv[2] }, { "options", body } })).then(do_print).then(do_close).fail(do_fail);
        } break;
        case Mode::wait: {
          instance
              .on("stopped",
                  [=](json data) {
                    auto name = data["service"];
                    if (argv[2] == name) instance.call("erase", data).then(do_print).then(do_close).fail(do_fail);
                  })
              .fail(do_fail);
        } break;
        case Mode::log: {
          instance
              .on("output",
                  [=](json data) {
                    if (data["service"] == argv[2]) std::cout << data["data"].get<std::string>() << std::flush;
                  })
              .fail(do_fail);
        } break;
        case Mode::attach: {
          auto update_size = [=] {
            struct winsize size;
            ioctl(STDIN_FILENO, TIOCGWINSZ, &size);
            instance.call("resize", json::object({ { "service", argv[2] }, { "column", size.ws_col }, { "row", size.ws_row } }));
          };
          auto sin = handler.reg([=](auto e) {
            int nread;
            ioctl(STDIN_FILENO, FIONREAD, &nread);
            if (nread <= 0) {
              instance.stop();
              handler.shutdown();
            }
            char buf[nread];
            read(STDIN_FILENO, buf, nread);
            instance.call("send", { { "service", argv[2] }, { "data", std::string{ buf, (size_t)nread } } });
          });
          handler.add(EPOLLIN, STDIN_FILENO, sin);
          auto sws = handler.reg([=](epoll_event const &e) {
            signalfd_siginfo info;
            read(e.data.fd, &info, sizeof info);
            update_size();
          });
          sigset_t ss;
          sigemptyset(&ss);
          sigaddset(&ss, SIGWINCH);
          sigprocmask(SIG_BLOCK, &ss, nullptr);
          auto sfd = signalfd(-1, &ss, SFD_CLOEXEC);
          handler.add(EPOLLIN, sfd, sws);
          struct termios term;
          if (tcgetattr(STDIN_FILENO, &term) != 0) {
            std::cerr << "terminal is required" << std::endl;
            instance.stop();
            handler.shutdown();
            return;
          }
          term.c_lflag &= ~(ECHO | ICANON);
          term.c_lflag |= IUTF8;
          tcsetattr(STDIN_FILENO, TCSAFLUSH, &term);
          update_size();
          instance
              .on("output",
                  [=](json data) {
                    if (data["service"] == argv[2]) std::cout << data["data"].get<std::string>() << std::flush;
                  })
              .fail(do_fail);
          instance
              .on("started",
                  [=](json data) {
                    if (data["service"] == argv[2]) update_size();
                  })
              .fail(do_fail);
        } break;
        default: {
          instance.stop();
          handler.shutdown();
        } break;
        }
      })
      .fail(do_fail);
  handler.wait();
}

void printHelp() {
  std::cout << "nsctl" << std::endl;
  std::cout << "- help                    print this message" << std::endl;
  std::cout << "- version                 print version" << std::endl;
  std::cout << "- shutdown                shutdown the server" << std::endl;
  std::cout << "- log [service]           monitor service's log" << std::endl;
  std::cout << "- status [service]        show runtime status of services" << std::endl;
  std::cout << "- start <service>         start service (configuation is read from stdin)" << std::endl;
  std::cout << "- stop <service>          send SIGTERM to service" << std::endl;
  std::cout << "- kill <service> <signal> send signal (number) to service" << std::endl;
  std::cout << "- erase <service>         erase service (must be exited state)" << std::endl;
  std::cout << "- send <service>          send text to service" << std::endl;
  std::cout << "- attach <service>        attach to service" << std::endl;
}