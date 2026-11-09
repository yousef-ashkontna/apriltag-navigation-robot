#pragma once
// Host tests only: ESP32 IRAM placement has no meaning on a desktop.
// No detector or pose behavior is replaced by this header.
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif
