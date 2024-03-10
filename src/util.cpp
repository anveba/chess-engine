#include "util.h"

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
