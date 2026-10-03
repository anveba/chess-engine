#include <algorithm>
#include <fstream>

#include "board.h"
#include "eval.h"
#include "movegen.h"
#include "moveorder.h"
#include "testing.h"

TEST(eval_is_colour_symmetric)
{
    std::ifstream in(testing::data_path("positions.txt"));
    CHECK(in.good());

    Board board;
    std::string fen;
    int count = 0;
    while (std::getline(in, fen)) {
        if (fen.empty())
            continue;
        board.set_fen(fen);
        const BoardEval eval = evaluate(board);
        board.set_fen(board.mirrored_fen());
        CHECK_EQ_CTX(evaluate(board), eval, fen);
        count++;
    }
    CHECK(count > 0); // check we actually tested something
}

TEST(eval_trace_matches_evaluate)
{
    Board board;
    for (const char* fen : { START_FEN, "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4",
                             "8/8/3k4/8/8/8/2R5/4K3 w - - 0 1", "8/8/3kr3/8/8/8/2B5/2R1K3 w - - 0 1" }) {
        board.set_fen(fen);
        const std::string trace = eval_trace(board);
        const std::string marker = "Final (side to move): ";
        const size_t pos = trace.find(marker);
        CHECK(pos != std::string::npos);
        if (pos != std::string::npos)
            CHECK_EQ_CTX(std::stoi(trace.substr(pos + marker.size())), int(evaluate(board)), fen);
    }
}

TEST(eval_basic_sanity)
{
    Board board;

    // An extra queen
    board.set_fen("4k3/8/8/8/8/8/8/3QK3 w - - 0 1");
    CHECK(evaluate(board) > 500);
    board.set_fen("4k3/8/8/8/8/8/8/3QK3 b - - 0 1");
    CHECK(evaluate(board) < -500);

    // King and bishop against king, scaled to a draw
    board.set_fen("4k3/8/8/8/8/8/8/2B1K3 w - - 0 1");
    CHECK(evaluate(board) < 100);
}

static Move legal_move(const Board& board, const std::string& uci)
{
    MoveList moves;
    moves.generate<ALL_LEGAL_MOVES>(board);
    for (Move m : moves)
        if (m.uci_notation() == uci)
            return m;
    return Move::make_none();
}

TEST(see_matches_known_exchanges)
{
    struct Case
    {
        const char* fen;
        const char* move;
        MoveEval value;
    };
    const Case cases[] = {
        // The traces on https://www.chessprogramming.org/SEE_-_The_Swap_Algorithm, with a knight worth 320.
        { "1k1r4/1pp4p/p7/4p3/8/P5P1/1PP4P/2K1R3 w - - 0 1", "e1e5", 100 },
        { "1k1r3q/1ppn3p/p4b2/4p3/8/P2N2P1/1PP1R1BP/2K1Q3 w - - 0 1", "d3e5", -220 },
        { "3qk3/3r4/8/3p4/8/8/3R4/3QK3 w - - 0 1", "d2d5", -400 },
        { "r1bq1rk1/1ppn1pbp/3ppnp1/p7/2PP1B2/P1N1PN2/1P2BPPP/R2QK2R w KQ - 0 9", "f3e5", -220 },
        { "4k3/8/2n5/3p4/8/5N2/8/4K3 w - - 0 1", "f3d4", -320 },
        { "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", "e5d6", 100 },
        { "3rk3/2P5/8/8/8/8/8/4K3 w - - 0 1", "c7d8q", 400 },
        { "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1g1", 0 },
    };

    Board board;
    for (const Case& c : cases) {
        CHECK(board.set_fen(c.fen));
        const Move move = legal_move(board, c.move);
        CHECK_CTX(!move.is_none(), c.fen);
        if (move.is_none())
            continue;
        CHECK_CTX(see_threshold(board, move, c.value), std::string(c.fen) + " " + c.move);
        CHECK_CTX(!see_threshold(board, move, c.value + 1), std::string(c.fen) + " " + c.move);
    }
}

// Raising the threshold can only turn the answer from true to false.
TEST(see_is_monotonic_in_threshold)
{
    const MoveEval thresholds[] = { -10000, -1000, -580, -330, -320, -230, -220, -100, -1, 0, 1, 100, 220, 320, 330, 500, 900, 1000 };

    std::ifstream in(testing::data_path("positions.txt"));
    CHECK(in.good());

    Board board;
    std::string fen;
    int moves_checked = 0;
    for (int line = 0; std::getline(in, fen); line++) {
        if (fen.empty() || line % 10 != 0 || !board.set_fen(fen))
            continue;
        MoveList moves;
        moves.generate<ALL_LEGAL_MOVES>(board);
        for (Move m : moves) {
            bool previous = true;
            for (MoveEval t : thresholds) {
                const bool result = see_threshold(board, m, t);
                CHECK_CTX(previous || !result, fen + " " + m.uci_notation() + " threshold " + std::to_string(t));
                previous = result;
            }
            moves_checked++;
        }
    }
    CHECK(moves_checked > 1000);
}

TEST(move_picker_hands_out_every_move_once)
{
    std::ifstream in(testing::data_path("positions.txt"));
    CHECK(in.good());

    Board board;
    std::string fen;
    for (int line = 0; std::getline(in, fen); line++) {
        if (fen.empty() || line % 10 != 0 || !board.set_fen(fen))
            continue;

        MoveList moves;
        moves.generate<ALL_LEGAL_MOVES>(board);
        std::vector<uint16_t> expected;
        for (Move m : moves)
            expected.push_back(m.encoding());
        const Move first = moves.at(moves.size() / 2);

        MovePicker picker(board, moves, first);
        std::vector<uint16_t> picked;
        for (Move m = picker.next(); !m.is_none(); m = picker.next())
            picked.push_back(m.encoding());

        CHECK_CTX(!picked.empty() && picked[0] == first.encoding(), fen);
        std::sort(expected.begin(), expected.end());
        std::sort(picked.begin(), picked.end());
        CHECK_CTX(picked == expected, fen);
    }
}
