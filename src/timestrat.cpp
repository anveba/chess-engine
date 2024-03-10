#include "timestrat.h"

#include <chrono>
#include <thread>

namespace TimeStrategy {

float one_twentieth(float wtime, float btime, float winc, float binc, int moves_to_go)
{
    return wtime / 20.0f;
}

}