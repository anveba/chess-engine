#include "perft.h"
#include "testing.h"

struct PerftCase
{
    const char* fen;
    int depth;
    uint64_t nodes;
};

// Standard positions plus Martin Sedlak's edge cases (en passant, castling, promotion, stalemate).
static const PerftCase PERFT_CASES[] = {
    { "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 4, 197281 },
    { "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 3, 97862 },
    { "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 5, 674624 },
    { "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 4, 422333 },
    { "r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1", 4, 422333 },
    { "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 3, 62379 },
    { "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 3, 89890 },
    { "3k4/3p4/8/K1P4r/8/8/8/8 b - - 0 1", 6, 1134888 },
    { "8/8/4k3/8/2p5/8/B2P2K1/8 w - - 0 1", 6, 1015133 },
    { "5k2/8/8/8/8/8/8/4K2R w K - 0 1", 6, 661072 },
    { "3k4/8/8/8/8/8/8/R3K3 w Q - 0 1", 6, 803711 },
    { "r3k2r/1b4bq/8/8/8/8/7B/R3K2R w KQkq - 0 1", 4, 1274206 },
    { "8/8/1P2K3/8/2n5/1q6/8/5k2 b - - 0 1", 5, 1004658 },
    { "4k3/1P6/8/8/8/8/K7/8 w - - 0 1", 6, 217342 },
    { "8/P1k5/K7/8/8/8/8/8 w - - 0 1", 6, 92683 },
    { "K1k5/8/P7/8/8/8/8/8 w - - 0 1", 6, 2217 },
    { "8/k1P5/8/1K6/8/8/8/8 w - - 0 1", 7, 567584 },
    { "8/8/2k5/5q2/5n2/8/5K2/8 b - - 0 1", 4, 23527 },
};

TEST(perft_known_counts)
{
    Board board;
    for (const PerftCase& c : PERFT_CASES) {
        board.set_fen(c.fen);
        CHECK_EQ_CTX(perft(board, c.depth).nodes, c.nodes, c.fen);
        CHECK_EQ_CTX(board.fen(), std::string(c.fen), "board restored after perft");
    }
}
