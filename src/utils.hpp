#pragma once

#include <string>

std::string GetEnvironmentVariableOrDefault(const std::string &variable_name, const std::string &default_value) {
  const char *value = getenv(variable_name.c_str());
  return value ? value : default_value;
}

#define LOAD_ENV(env, def) static const auto env = GetEnvironmentVariableOrDefault(#env, def)