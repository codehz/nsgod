#pragma once

#include <chrono>
#include <fstream>
#include <memory>
#include <map>
#include <rpc.hpp>
#include <string>
#include <vector>

enum struct ProcessStatus {
  Waiting,
  Running,
  Stopped,
  Exited,
};

NLOHMANN_JSON_SERIALIZE_ENUM(ProcessStatus, {
                                                { ProcessStatus::Waiting, "waiting" },
                                                { ProcessStatus::Running, "running" },
                                                { ProcessStatus::Stopped, "stoped" },
                                                { ProcessStatus::Exited, "exited" },
                                            });

struct ProcessLaunchOptions {
  bool waitstop, pty;
  std::string root, cwd, log;
  std::vector<std::string> cmdline, env;
  std::map<std::string, std::string> mounts;
};

struct ProcessInfo {
  pid_t pid;
  ProcessStatus status;
  std::chrono::system_clock::time_point start_time, dead_time;
  ProcessLaunchOptions options;
  int fd, log;
};

struct ProcessInfoClient {
  pid_t pid;
  ProcessStatus status;
  std::chrono::system_clock::time_point start_time, dead_time;
  ProcessLaunchOptions options;
};

inline void to_json(rpc::json &j, const ProcessLaunchOptions &i) {
  j["waitstop"] = i.waitstop;
  j["pty"]      = i.pty;
  j["cmdline"]  = i.cmdline;
  j["root"]     = i.root;
  j["cwd"]      = i.cwd;
  j["log"]      = i.log;
  j["env"]      = i.env;
  j["mounts"]   = i.mounts;
}

inline void from_json(const rpc::json &j, ProcessLaunchOptions &i) {
  j.at("cmdline").get_to(i.cmdline);
  i.waitstop = j.value("waitstop", false);
  i.pty      = j.value("pty", false);
  i.root     = j.value("root", "/");
  i.cwd      = j.value("cwd", ".");
  i.log      = j.value("log", "");
  i.env      = j.value("env", std::vector<std::string>{});
  i.mounts   = j.value("mounts", std::map<std::string, std::string>{});
}

namespace nlohmann {
template <> struct adl_serializer<std::chrono::system_clock::time_point> {
  static void to_json(json &j, const std::chrono::system_clock::time_point &i) { j = std::chrono::system_clock::to_time_t(i); }
  static void from_json(const json &j, std::chrono::system_clock::time_point &i) { i = std::chrono::system_clock::from_time_t(j.get<std::time_t>()); }
};
} // namespace nlohmann

inline void to_json(rpc::json &j, const ProcessInfo &i) {
  j["pid"]        = i.pid;
  j["status"]     = i.status;
  j["start_time"] = i.start_time;
  j["dead_time"]  = i.dead_time;
  j["options"]    = i.options;
}

inline void from_json(rpc::json const &j, ProcessInfoClient &i) {
  j.at("pid").get_to(i.pid);
  j.at("status").get_to(i.status);
  j.at("start_time").get_to(i.start_time);
  j.at("dead_time").get_to(i.dead_time);
  j.at("options").get_to(i.options);
}

int init(bool debug);
ProcessInfo createProcess(ProcessLaunchOptions options);