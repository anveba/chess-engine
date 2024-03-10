#include <iostream>

#include "board.h"
#include "uci.h"

int main(int argc, char** argv)
{
    precompute_bb();
    precompute_zobrist();

    UCI().start();
}