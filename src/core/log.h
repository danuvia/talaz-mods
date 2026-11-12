#pragma once

// The client's log: the launcher's log on Android, a file next to the settings on Windows
#ifdef __ANDROID__
#include <android/log.h>
#define log_info(...) __android_log_print(ANDROID_LOG_INFO, CLIENT_NAME, __VA_ARGS__)
#define log_error(...) __android_log_print(ANDROID_LOG_ERROR, CLIENT_NAME, __VA_ARGS__)
#else
void log_write(const char* level, const char* format, ...);
#define log_info(...) log_write("Info", __VA_ARGS__)
#define log_error(...) log_write("Error", __VA_ARGS__)
#endif
