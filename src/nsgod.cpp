#include <csignal>
#include <iostream>
#include <memory>
#include <rpcws.hpp>
#include <signal.h>
#include <stropts.h>
#include <sys/ioctl.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include "process.h"
#include "utils.hpp"

LOAD_ENV(NSGOD_API, "ws+unix://nsgod.socket");
LOAD_ENV(NSGOD_LOCK, "nsgod.lock");

std::map<std::string, ProcessInfo> status_map;
std::map<int, std::string> fdmap;
std::map<int, std::string> pidmap;

int main() {
  using namespace rpcws;
  try {
    int ev = init(getenv("NSGOD_DEBUG"));
    lockfile(NSGOD_LOCK);
    auto ep = std::make_shared<epoll>();
    static RPC instance{ std::make_unique<server_wsio>(NSGOD_API, ep) };
    static auto &handler = *ep;

    static auto subproc = handler.reg([](epoll_event const &e) {
      static char buffer[0xFFFF];
      if (e.events & EPOLLERR || e.events & EPOLLHUP) {
        handler.del(e.data.fd);
        fdmap.erase(e.data.fd);
        close(e.data.fd);
        auto srv = fdmap[e.data.fd];
        if (auto it = status_map.find(srv); it != status_map.end()) {
          auto &status = it->second;
          if (status.log) close(status.log);
        }
        return;
      }

      ssize_t count = read(e.data.fd, buffer, sizeof buffer);
      std::string_view data{ buffer, (size_t)count };
      auto srv = fdmap[e.data.fd];
      if (auto it = status_map.find(srv); it != status_map.end()) {
        auto &status = it->second;
        if (status.log) write(status.log, buffer, count);
        instance.emit("output", json::object({ { "service", srv }, { "data", std::string{ buffer, (size_t)count } } }));
      }
    });

    instance.event("output");
    instance.event("started");
    instance.event("stopped");
    instance.event("updated");

    instance.reg("ping", [](auto client, json data) -> json { return data; });
    instance.reg("version", [](auto client, json data) -> json { return "v0.1.0"; });
    instance.reg("start", [&](auto client, json data) -> json {
      ProcessLaunchOptions opts;
      auto name = data["service"].get<std::string>();
      data["options"].get_to(opts);
      if (auto it = status_map.find(name); it != status_map.end()) {
        if (it->second.status == ProcessStatus::Exited) {
          handler.del(it->second.fd);
          fdmap.erase(it->second.fd);
          pidmap.erase(it->second.pid);
          status_map.erase(it);
        } else
          throw std::runtime_error("target service exists and not exited.");
      }
      auto proc = createProcess(opts);
      status_map.emplace(name, proc);
      fdmap[proc.fd]   = name;
      pidmap[proc.pid] = name;
      handler.add(EPOLLIN, proc.fd, subproc);
      return proc;
    });
    instance.reg("send", [&](auto client, json data) -> json {
      auto name    = data["service"].get<std::string>();
      auto content = data["data"].get<std::string>();
      if (auto it = status_map.find(name); it != status_map.end()) {
        if (it->second.status == ProcessStatus::Exited) throw std::runtime_error("target service exited.");
        write(it->second.fd, content.data(), content.size());
        return json::object({ { name, "ok" } });
      } else
        throw std::runtime_error("target service not exists.");
    });
    instance.reg("resize", [&](auto client, json data) -> json {
      auto name = data["service"].get<std::string>();
      if (auto it = status_map.find(name); it != status_map.end()) {
        if (it->second.status == ProcessStatus::Exited) throw std::runtime_error("target service exited.");
        winsize ws;
        ioctl(it->second.fd, TIOCGWINSZ, &ws);
        ws.ws_col = data.value("column", ws.ws_col);
        ws.ws_row = data.value("row", ws.ws_row);
        ioctl(it->second.fd, TIOCSWINSZ, &ws);
        return json::object({ { name, "ok" } });
      } else
        throw std::runtime_error("target service not exists.");
    });
    instance.reg("erase", [&](auto client, json data) -> json {
      auto name = data["service"].get<std::string>();
      if (auto it = status_map.find(name); it != status_map.end()) {
        if (it->second.status != ProcessStatus::Exited) throw std::runtime_error("target service not exited.");
        handler.del(it->second.fd);
        fdmap.erase(it->second.fd);
        pidmap.erase(it->second.pid);
        status_map.erase(it);
        return json::object({ { name, "ok" } });
      } else
        throw std::runtime_error("target service not exists.");
    });
    instance.reg("status", [](auto client, json data) -> json {
      if (data.contains("service")) {
        auto name = data["service"].get<std::string>();
        if (status_map.find(name) == status_map.end()) throw std::runtime_error("target service not exists.");
        return status_map[name];
      } else {
        return status_map;
      }
    });
    instance.reg("kill", [](auto client, json data) -> json {
      auto name    = data["service"].get<std::string>();
      auto sig     = data["signal"].get<int>();
      auto restart = data.value("restart", RestartMode::Normal);
      if (auto it = status_map.find(name); it == status_map.end())
        throw std::runtime_error("target service not exists.");
      else {
        it->second.restart_mode = restart;
        if (kill(it->second.pid, sig) != 0) throw std::runtime_error(strerror(errno));
      }
      return nullptr;
    });
    instance.reg("shutdown", [](auto client, json data) -> json {
      kill(getpid(), SIGINT);
      return nullptr;
    });

    if (ev != -1) {
      uint64_t x = 1;
      write(ev, &x, sizeof x);
      close(ev);
    }

    {
      sigset_t ss;
      sigemptyset(&ss);
      sigaddset(&ss, SIGINT);
      sigaddset(&ss, SIGCHLD);
      sigprocmask(SIG_BLOCK, &ss, nullptr);
      auto sfd = signalfd(-1, &ss, SFD_CLOEXEC);
      handler.add(EPOLLIN, sfd, handler.reg([=](epoll_event const &e) {
        signalfd_siginfo info;
        read(e.data.fd, &info, sizeof info);
        switch (info.ssi_signo) {
        case SIGINT: {
          instance.stop();
          handler.shutdown();
        } break;
        case SIGCHLD: {
          int wstatus;
          auto pid = waitpid(WAIT_ANY, &wstatus, WNOHANG | WUNTRACED | WCONTINUED);
          if (pid <= 0) return;
          auto service = pidmap[pid];
          auto &info   = status_map[service];
          if (WIFSTOPPED(wstatus)) {
            if (info.options.waitstop && info.status == ProcessStatus::Waiting) {
              kill(pid, SIGCONT);
              instance.emit("started", json::object({ { "service", service } }));
              info.status = ProcessStatus::Running;
            } else
              info.status = ProcessStatus::Stopped;
          } else if (WIFCONTINUED(wstatus)) {
            info.status = ProcessStatus::Running;
          } else {
            info.status    = ProcessStatus::Exited;
            auto last      = info.dead_time;
            info.dead_time = std::chrono::system_clock::now();
            pidmap.erase(pid);
            close(info.log);
            if (info.restart_mode == RestartMode::Force || info.options.restart.enabled) {
              if (info.restart_mode == RestartMode::Normal && info.dead_time - last > info.options.restart.reset_timer) { info.restart = 0; }
              if (info.restart_mode == RestartMode::Prevent ||
                  (info.restart_mode == RestartMode::Normal && info.restart++ >= info.options.restart.max)) {
                instance.emit("stopped", json::object({
                                             { "service", service },
                                             { "restart", json::object({
                                                              { "error", "max" },
                                                          }) },
                                         }));
              } else {
                info.restart_mode = RestartMode::Normal;
                try {
                  auto proc = createProcess(info.options);
                  handler.del(info.fd);
                  fdmap.erase(info.fd);
                  info.fd          = proc.fd;
                  info.pid         = proc.pid;
                  info.start_time  = proc.start_time;
                  info.status      = proc.status;
                  info.log         = proc.log;
                  fdmap[proc.fd]   = service;
                  pidmap[proc.pid] = service;
                  handler.add(EPOLLIN, proc.fd, subproc);
                  instance.emit("stopped", json::object({
                                               { "service", service },
                                               { "restart", json::object({
                                                                { "max", info.options.restart.max },
                                                                { "current", info.restart },
                                                            }) },
                                           }));
                } catch (std::exception &x) {
                  instance.emit("stopped", json::object({
                                               { "service", service },
                                               { "restart", json::object({
                                                                { "error", "failed to restart" },
                                                            }) },
                                           }));
                }
              }
            } else {
              instance.emit("stopped", json::object({ { "service", service } }));
            }
          }
          instance.emit("updated", status_map);
        } break;
        }
      }));
    }

    instance.start();
    handler.wait();
  } catch (std::runtime_error &e) { std::cerr << e.what() << std::endl; }
}