#include "util.h"

#include <emmintrin.h>
#include <iostream>
#include <mutex>

void log_sync(const std::string& str)
{
    static std::mutex log_mutex;

    log_mutex.lock();

    std::cout << str;
    std::cout.flush();

    log_mutex.unlock();
}

void log_error_sync(const std::string& str)
{
    static std::mutex log_mutex;

    log_mutex.lock();

    std::cerr << str;
    std::cerr.flush();

    log_mutex.unlock();
}

void prefetch(void* ptr)
{
    __builtin_prefetch(ptr);
}