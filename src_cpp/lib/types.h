#pragma once

#include <mutex>
#include <string>

#define DISABLE_COPY(T) \
	T(const T &) = delete; \
	T &operator=(const T &) = delete;

typedef std::unique_lock<std::recursive_mutex> MutexLockR;
typedef std::unique_lock<std::mutex> MutexLock;
typedef const std::string cstr_t;
