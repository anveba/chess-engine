#ifndef NO_MAIN

#include <iostream>

#include "board.h"
#include "uci.h"

int main(int argc, char** argv)
{
    precompute_bitboards();
    precompute_board_constants();

    UCI().start();
}
#endif