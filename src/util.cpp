#include "util.h"

#include <emmintrin.h>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>

static std::mutex debug_log_mutex;
static std::ofstream debug_log;

static void write_debug_log(const std::string& prefix, const std::string& text)
{
    std::lock_guard<std::mutex> guard(debug_log_mutex);
    if (!debug_log.is_open())
        return;

    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line))
        debug_log << prefix << line << "\n";
    debug_log.flush();
}

void set_debug_log(const std::string& path)
{
    std::lock_guard<std::mutex> guard(debug_log_mutex);
    debug_log.close();
    if (!path.empty())
        debug_log.open(path, std::ios::app);
}

void debug_log_input(const std::string& line)
{
    write_debug_log("< ", line);
}

void log_sync(const std::string& str)
{
    static std::mutex log_mutex;

    log_mutex.lock();

    std::cout << str;
    std::cout.flush();

    log_mutex.unlock();

    write_debug_log("> ", str);
}

void log_error_sync(const std::string& str)
{
    static std::mutex log_mutex;

    log_mutex.lock();

    std::cerr << str;
    std::cerr.flush();

    log_mutex.unlock();

    write_debug_log("! ", str);
}

void prefetch(void* ptr)
{
    __builtin_prefetch(ptr);
}