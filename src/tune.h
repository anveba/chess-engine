#ifndef TUNE_H_INCLUDED
#define TUNE_H_INCLUDED

#include <string>
#include <vector>

class UCIOption; // Not included, since uci.h includes headers that use TUNABLE.

struct Tunable
{
    std::string name;
    int* value;
    int min, max;
};

void register_tunable(const Tunable& tunable);
std::vector<UCIOption> tunable_options();

#ifdef TUNE
#define TUNABLE(name, default_value, min, max) \
    inline int name = default_value;           \
    inline const bool name##_registered = (register_tunable({ #name, &name, min, max }), true)
#else
#define TUNABLE(name, default_value, min, max) constexpr int name = default_value
#endif

#endif