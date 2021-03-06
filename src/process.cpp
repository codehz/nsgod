#include "process.h"
#include <fcntl.h>
#include <filesystem>
#include <pty.h>
#include <sys/eventfd.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

#define check_error(stmt)                                                                                                                            \
  if (!(stmt)) throw std::runtime_error(std::string(#stmt ":") + strerror(errno));

void write_sth_to_file(char const *path, char const *content, size_t len) {
  if (len == -1) len = strlen(content);
  int fd;
  check_error((fd = openat(AT_FDCWD, path, O_WRONLY)) >= 0);
  check_error(write(fd, content, len) == len);
  check_error(close(fd) == 0);
}

void deny_to_setgroups() { write_sth_to_file("/proc/self/setgroups", "deny", -1); }

void map_to_root(int id, char const *filename) {
  char buf[0x20] = { 0 };
  int len        = snprintf(buf, 0x100, "0 %d 1", id);
  write_sth_to_file(filename, buf, len);
}

int init(bool debug) {
  for (int i = 3; i < 256; i++) close(i);
  int fnull = open("/dev/null", O_RDWR);
  dup3(fnull, STDIN_FILENO, 0);
  dup3(fnull, STDOUT_FILENO, 0);
  dup3(fnull, STDERR_FILENO, 0);
  close(fnull);

  int ceuid = geteuid();
  int cegid = getegid();

  if (debug) {
    check_error(unshare(CLONE_NEWUSER | CLONE_NEWNS | CLONE_NEWIPC) == 0);
    deny_to_setgroups();
    map_to_root(ceuid, "/proc/self/uid_map");
    map_to_root(cegid, "/proc/self/gid_map");
    return -1;
  }

  check_error(unshare(CLONE_NEWUSER | CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWIPC) == 0);
  deny_to_setgroups();
  map_to_root(ceuid, "/proc/self/uid_map");
  map_to_root(cegid, "/proc/self/gid_map");

  int ev = eventfd(0, 0);

  auto ret = fork();
  check_error(ret >= 0);
  if (ret) {
    signal(SIGCHLD, [](auto) { exit(EXIT_FAILURE); });
    uint64_t val = 0;
    read(ev, &val, 8);
    exit(EXIT_SUCCESS);
  }
  setsid();
  check_error(mount("proc", "/proc", "proc", 0, nullptr) == 0);
  signal(SIGPIPE, SIG_IGN);

  return ev;
}

char *const *buildv(std::vector<std::string> &vec) {
  auto ret = new char *[vec.size() + 1];
  size_t i = 0;
  for (auto &item : vec) ret[i++] = item.data();
  ret[i] = nullptr;
  return ret;
}

inline __attribute__((always_inline)) void setup_env(ProcessLaunchOptions const &options) {
  auto root = fs::path{ options.root };
  for (auto &[k, v] : options.mounts) {
    auto src = root / k;
    auto tgt = fs::path{ v };
    mount(tgt.c_str(), src.c_str(), "tmpfs", MS_BIND | MS_REC, nullptr);
  }
  chroot(root.c_str());
  chdir(options.cwd.c_str());
}

ProcessInfo createProcess(ProcessLaunchOptions options) {
  ProcessInfo ret{
    .start_time = std::chrono::system_clock::now(),
    .options    = options,
  };
  if (!options.log.empty()) {
    ret.log = open64(options.log.c_str(), O_WRONLY | O_APPEND | O_CLOEXEC | O_CREAT, 0600);
    if (ret.log == -1) throw std::runtime_error("failed to open log file");
  }
  if (options.pty) {
    auto pid = forkpty(&ret.fd, nullptr, nullptr, nullptr);
    if (pid < 0) throw std::runtime_error("failed to fork");
    if (pid == 0) {
      setup_env(options);

      auto argv = buildv(options.cmdline);
      auto envp = buildv(options.env);

      _exit(execvpe(argv[0], argv, envp));
    }
    ret.pid    = pid;
    ret.status = options.waitstop ? ProcessStatus::Waiting : ProcessStatus::Running;
  } else {
    int fds[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    auto pid = fork();
    if (pid < 0) throw std::runtime_error("failed to fork");
    if (pid == 0) {
      close(fds[0]);
      dup2(fds[1], 0);
      dup2(fds[1], 1);
      dup2(fds[1], 2);
      close(fds[1]);
      setup_env(options);

      auto argv = buildv(options.cmdline);
      auto envp = buildv(options.env);

      _exit(execvpe(argv[0], argv, envp));
    }
    close(fds[1]);
    ret.pid    = pid;
    ret.status = options.waitstop ? ProcessStatus::Waiting : ProcessStatus::Running;
    ret.fd     = fds[0];
  }
  return ret;
}