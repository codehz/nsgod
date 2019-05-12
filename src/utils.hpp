#pragma once

#include <fcntl.h>
#include <string>

std::string GetEnvironmentVariableOrDefault(const std::string &variable_name, const std::string &default_value) {
  const char *value = getenv(variable_name.c_str());
  return value ? value : default_value;
}

#define LOAD_ENV(env, def) static const auto env = GetEnvironmentVariableOrDefault(#env, def)

void lockfile(std::string const &name) {
  if (auto fd = creat(name.c_str(), 0755); fd != -1) {
    if (lockf(fd, F_TLOCK, 0) != 0) {
      fprintf(stderr, "Failed to lock %s\n", name.c_str());
      abort();
    }
  } else {
    perror("creat(lock)");
    abort();
  }
}