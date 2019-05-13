#include <csignal>
#include <iostream>
#include <memory>
#include <rpcws.hpp>
#include <stropts.h>
#include <sys/ioctl.h>
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
    int ev = init();
    lockfile(NSGOD_LOCK);
    static RPC instance{ std::make_unique<server_wsio>(NSGOD_API) };
    static auto &handler = static_cast<server_wsio &>(instance.layer()).handler();
    signal(SIGINT, [](auto) { instance.stop(); });
    signal(SIGCHLD, [](auto) {
      int wstatus;
      auto pid = waitpid(WAIT_ANY, &wstatus, WNOHANG | WUNTRACED | WCONTINUED);
      if (pid <= 0) return;
      auto service = pidmap[pid];
      auto &info   = status_map[service];
      if (WSTOPSIG(wstatus)) {
        if (info.options.waitstop && info.status == ProcessStatus::Waiting)
          kill(pid, SIGCONT);
        else
          info.status = ProcessStatus::Stopped;
      } else if (WIFCONTINUED(wstatus)) {
        if (info.options.waitstop && info.status == ProcessStatus::Waiting) instance.emit("started", json::object({ { "service", service } }));
        info.status = ProcessStatus::Running;
      } else {
        info.status    = ProcessStatus::Exited;
        info.dead_time = std::chrono::system_clock::now();
        pidmap.erase(pid);
        instance.emit("stopped", json::object({ { "service", service } }));
      }
    });

    auto subproc = handler.reg([](epoll_event const &e) {
      static char buffer[0xFFFF];
      if (e.events & EPOLLERR || e.events & EPOLLHUP) {
        handler.del(e.data.fd);
        fdmap.erase(e.data.fd);
        close(e.data.fd);
        return;
      }

      size_t count = ::recv(e.data.fd, buffer, sizeof buffer, 0);
      instance.emit("output", json::object({ { "service", fdmap[e.data.fd] }, { "data", std::string_view{ buffer, count } } }));
    });

    instance.event("output");
    instance.event("started");
    instance.event("stopped");

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
      auto name = data["service"].get<std::string>();
      auto sig  = data["signal"].get<int>();
      if (status_map.find(name) == status_map.end()) throw std::runtime_error("target service not exists.");
      if (kill(status_map[name].pid, sig) != 0) throw std::runtime_error(strerror(errno));
      return nullptr;
    });
    instance.reg("shutdown", [](auto client, json data) -> json {
      instance.stop();
      return nullptr;
    });

    {
      uint64_t x = 1;
      write(ev, &x, 8);
      close(ev);
    }

    instance.start();
  } catch (std::runtime_error &e) { std::cerr << e.what() << std::endl; }
}