#pragma once
#include <stdint.h>
#include <string.h>

class Test1000 {
public:
  static constexpr uint32_t durationMs = 90000;
  char id[33] = {};

  bool start(const char* session, uint32_t now) {
    if (!session || strlen(session) != 32 || remaining(now) || !strcmp(id, session)) return false;
    for (size_t i = 0; i < 32; ++i) {
      char c = session[i];
      if (!(('0' <= c && c <= '9') || ('a' <= c && c <= 'f'))) return false;
    }
    memcpy(id, session, sizeof(id));
    started = now;
    active = true;
    return true;
  }

  uint32_t remaining(uint32_t now) {
    uint32_t elapsed = now - started;
    if (!active || elapsed >= durationMs) {
      active = false;
      return 0;
    }
    return durationMs - elapsed;
  }

  void stop() { active = false; }

private:
  uint32_t started = 0;
  bool active = false;
};
