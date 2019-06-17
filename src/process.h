#pragma once

#include <chrono>
#include <fstream>
#include <map>
#include <memory>
#include <rpc.hpp>
#include <string>
#include <vector>

enum struct ProcessStatus {
  Waiting,
  Running,
  Stopped,
  Exited,
  Restarting,
};

NLOHMANN_JSON_SERIALIZE_ENUM(ProcessStatus, {
                                                { ProcessStatus::Waiting, "waiting" },
                                                { ProcessStatus::Running, "running" },
                                                { ProcessStatus::Stopped, "stoped" },
                                                { ProcessStatus::Exited, "exited" },
                                                { ProcessStatus::Restarting, "restarting" },
                                            });

struct RestartPolicy {
  bool enabled;
  int max;
  std::chrono::milliseconds reset_timer;
};

struct ProcessLaunchOptions {
  bool waitstop, pty;
  std::string root, cwd, log;
  std::vector<std::string> cmdline, env;
  std::map<std::string, std::string> mounts;
  RestartPolicy restart;
};

struct ProcessInfo {
  pid_t pid;
  ProcessStatus status;
  int restart;
  std::chrono::system_clock::time_point start_time, dead_time;
  ProcessLaunchOptions options;
  int fd, log;
};

struct ProcessInfoClient {
  pid_t pid;
  ProcessStatus status;
  int restart;
  std::chrono::system_clock::time_point start_time, dead_time;
  ProcessLaunchOptions options;
};

namespace nlohmann {
template <> struct adl_serializer<std::chrono::system_clock::time_point> {
  static void to_json(json &j, const std::chrono::system_clock::time_point &i) { j = std::chrono::system_clock::to_time_t(i); }
  static void from_json(const json &j, std::chrono::system_clock::time_point &i) { i = std::chrono::system_clock::from_time_t(j.get<std::time_t>()); }
};

template <class Rep, class Period> struct adl_serializer<std::chrono::duration<Rep, Period>> {
  static void to_json(json &j, const std::chrono::duration<Rep, Period> &i) { j = i.count(); }
  static void from_json(const json &j, std::chrono::duration<Rep, Period> &i) { i = std::chrono::duration<Rep, Period>(j.get<Rep>()); }
};
} // namespace nlohmann

inline void to_json(rpc::json &j, const RestartPolicy &i) {
  j["enabled"]     = i.enabled;
  j["max"]         = i.max;
  j["reset_timer"] = i.reset_timer;
}

inline void from_json(const rpc::json &j, RestartPolicy &i) {
  j.at("enabled").get_to(i.enabled);
  j.at("max").get_to(i.max);
  j.at("reset_timer").get_to(i.reset_timer);
}

inline void to_json(rpc::json &j, const ProcessLaunchOptions &i) {
  j["waitstop"] = i.waitstop;
  j["pty"]      = i.pty;
  j["cmdline"]  = i.cmdline;
  j["root"]     = i.root;
  j["cwd"]      = i.cwd;
  j["log"]      = i.log;
  j["env"]      = i.env;
  j["mounts"]   = i.mounts;
  j["restart"]  = i.restart;
}

inline void from_json(const rpc::json &j, ProcessLaunchOptions &i) {
  using namespace std::chrono;

  j.at("cmdline").get_to(i.cmdline);
  i.waitstop = j.value("waitstop", false);
  i.pty      = j.value("pty", false);
  i.root     = j.value("root", "/");
  i.cwd      = j.value("cwd", ".");
  i.log      = j.value("log", "");
  i.env      = j.value("env", std::vector<std::string>{});
  i.mounts   = j.value("mounts", std::map<std::string, std::string>{});
  i.restart  = j.value("restart", RestartPolicy{ false, 0, 0ms });
}

inline void to_json(rpc::json &j, const ProcessInfo &i) {
  j["pid"]        = i.pid;
  j["status"]     = i.status;
  j["start_time"] = i.start_time;
  j["dead_time"]  = i.dead_time;
  j["restart"]    = i.restart;
  j["options"]    = i.options;
}

inline void from_json(rpc::json const &j, ProcessInfoClient &i) {
  j.at("pid").get_to(i.pid);
  j.at("status").get_to(i.status);
  j.at("start_time").get_to(i.start_time);
  j.at("dead_time").get_to(i.dead_time);
  j.at("restart").get_to(i.restart);
  j.at("options").get_to(i.options);
}

int init(bool debug);
ProcessInfo createProcess(ProcessLaunchOptions options);