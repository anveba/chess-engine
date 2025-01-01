#ifndef UTIL_H_INCLUDED
#define UTIL_H_INCLUDED

#include <string>

void log_sync(const std::string& str);
void log_error_sync(const std::string& str);

constexpr bool implies(bool a, bool b)
{
    return !a || b;
}

constexpr bool iff(bool a, bool b)
{
    return implies(a, b) && implies(b, a);
}

template<typename T>
constexpr int sign(T x)
{
    return (T(0) < x) - (x < T(0));
}

void prefetch(void* ptr);

#endif