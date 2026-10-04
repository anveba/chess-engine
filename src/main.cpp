#ifndef NO_MAIN

#include <iostream>

#include "board.h"
#include "uci.h"

int main(int argc, char** argv)
{
    precompute_bitboards();
    precompute_board_constants();

#if !TRADITIONAL_EVAL
    NNUE nnue;
    if (NNUE::load(DEFAULT_NETWORK, nnue)) {
        set_nnue(nnue);
    } else {
        std::cerr << "Failed to load the default NNUE network." << std::endl;
        return 1;
    }
#endif

    UCI().start();
}
#endif