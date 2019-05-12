#include "process.h"
#include <fcntl.h>
#include <filesystem>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

void write_sth_to_file(char const *path, char const *content, size_t len) {
  if (len == -1) len = strlen(content);
  int fd;
  assert((fd = openat(AT_FDCWD, path, O_WRONLY)) >= 0);
  assert(write(fd, content, len) == len);
  assert(close(fd) == 0);
}

void deny_to_setgroups() { write_sth_to_file("/proc/self/setgroups", "deny", -1); }

void map_to_root(int id, char const *filename) {
  char buf[0x20] = { 0 };
  int len        = snprintf(buf, 0x100, "0 %d 1", id);
  write_sth_to_file(filename, buf, len);
}

void init() {
  int ceuid = geteuid();
  int cegid = getegid();

  assert(unshare(CLONE_NEWUSER | CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWIPC) == 0);
  deny_to_setgroups();
  map_to_root(ceuid, "/proc/self/uid_map");
  map_to_root(cegid, "/proc/self/gid_map");
  auto ret = fork();
  assert(ret >= 0);
  if (ret) {
    int exitcode;
    waitpid(WAIT_ANY, &exitcode, 0);
    exit(exitcode);
  }
  setsid();
  assert(mount("proc", "/proc", "proc", 0, nullptr) == 0);
  signal(SIGPIPE, SIG_IGN);
}

char *const *buildv(std::vector<std::string> &vec) {
  auto ret = new char *[vec.size() + 1];
  size_t i = 0;
  for (auto &item : vec) ret[i++] = item.data();
  ret[i] = nullptr;
  return ret;
}

ProcessInfo createProcess(ProcessLaunchOptions options) {
  ProcessInfo ret{
    .start_time = std::chrono::system_clock::now(),
    .options    = options,
  };
  int fds[2];
  socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
  auto pid = fork();
  assert(pid >= 0);
  if (!pid) {
    close(fds[0]);
    dup2(fds[1], 0);
    dup2(fds[1], 1);
    dup2(fds[1], 2);
    close(fds[1]);
    auto root = fs::path{ options.root };
    for (auto &[k, v] : options.mounts) {
      auto src = root / k;
      auto tgt = fs::path{ v };
      mount(tgt.c_str(), src.c_str(), "tmpfs", MS_BIND | MS_REC, nullptr);
    }
    chroot(root.c_str());
    chdir(options.cwd.c_str());

    auto argv = buildv(options.cmdline);
    auto envp = buildv(options.env);

    execvpe(argv[0], argv, envp);
  }
  close(fds[1]);
  ret.pid    = pid;
  ret.status = options.waitstop ? ProcessStatus::Waiting : ProcessStatus::Running;
  ret.fd     = fds[0];
  return ret;
}