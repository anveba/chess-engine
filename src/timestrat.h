#ifndef TIMESTRAT_H_INCLUDED
#define TIMESTRAT_H_INCLUDED

#include "chess.h"

#include <chrono>

using Ms = std::chrono::milliseconds::rep;

inline Ms now()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

struct SearchConditions;

class TimeManager
{
  public:
    bool check();

    void set(Colour side, const SearchConditions& conditions);
    void start();

  private:
    Ms start_time;
    Ms search_time;
};

#endif