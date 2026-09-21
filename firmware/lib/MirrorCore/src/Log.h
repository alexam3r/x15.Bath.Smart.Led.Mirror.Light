#pragma once
#if defined(ARDUINO) && defined(DEBUG_LOG_ENABLED) && DEBUG_LOG_ENABLED
  #include <Arduino.h>
  #define MLOG(...) do { Serial.printf(__VA_ARGS__); } while (0)
#else
  #define MLOG(...) do {} while (0)
#endif
