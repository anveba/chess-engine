#ifndef TESTING_H_INCLUDED
#define TESTING_H_INCLUDED

#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace testing {

struct TestCase
{
    std::string name;
    std::function<void()> run;
};

inline std::vector<TestCase>& registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failures()
{
    static int count = 0;
    return count;
}

struct Registrar
{
    Registrar(const std::string& name, std::function<void()> run) { registry().push_back({ name, run }); }
};

inline void report(const char* file, int line, const std::string& message)
{
    failures()++;
    std::cout << "  FAILED " << file << ":" << line << ": " << message << std::endl;
}

std::string data_path(const std::string& file);

}

#define TEST(name)                                           \
    static void name();                                      \
    static testing::Registrar name##_registrar(#name, name); \
    static void name()

#define CHECK(cond)                                                  \
    do {                                                             \
        if (!(cond))                                                 \
            testing::report(__FILE__, __LINE__, "CHECK(" #cond ")"); \
    } while (0)

#define CHECK_EQ(a, b)                                                             \
    do {                                                                           \
        auto check_a = (a);                                                        \
        auto check_b = (b);                                                        \
        if (!(check_a == check_b)) {                                               \
            std::ostringstream check_msg;                                          \
            check_msg << #a " == " #b " (" << check_a << " vs " << check_b << ")"; \
            testing::report(__FILE__, __LINE__, check_msg.str());                  \
        }                                                                          \
    } while (0)

#define CHECK_CTX(cond, ctx)                                      \
    do {                                                          \
        if (!(cond)) {                                            \
            std::ostringstream check_msg;                         \
            check_msg << "CHECK(" #cond ") for " << ctx;          \
            testing::report(__FILE__, __LINE__, check_msg.str()); \
        }                                                         \
    } while (0)

#define CHECK_EQ_CTX(a, b, ctx)                                                                \
    do {                                                                                       \
        auto check_a = (a);                                                                    \
        auto check_b = (b);                                                                    \
        if (!(check_a == check_b)) {                                                           \
            std::ostringstream check_msg;                                                      \
            check_msg << #a " == " #b " (" << check_a << " vs " << check_b << ") for " << ctx; \
            testing::report(__FILE__, __LINE__, check_msg.str());                              \
        }                                                                                      \
    } while (0)

#endif
