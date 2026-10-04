#include <deque>
#include <fstream>
#include <random>

#include "board.h"
#include "movegen.h"
#include "testing.h"

static Move find_move(Board& board, const std::string& uci)
{
    MoveList moves;
    moves.generate<ALL_LEGAL_MOVES>(board);
    for (Move m : moves)
        if (m.uci_notation() == uci)
            return m;
    return Move::make_none();
}

static bool accumulator_matches_refresh(const Board& board)
{
    Board refreshed;
    refreshed.set_fen(board.fen());
    return board.nnue_accumulator() == refreshed.nnue_accumulator();
}

static std::vector<std::string> playout_fens()
{
    std::vector<std::string> fens = {
        START_FEN,
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    };
    std::ifstream in(testing::data_path("positions.txt"));
    std::string line;
    for (int i = 0; std::getline(in, line); i++)
        if (!line.empty() && i % 100 == 0)
            fens.push_back(line);
    return fens;
}

TEST(random_playouts_keep_board_consistent)
{
    constexpr int PLAYOUTS_PER_POSITION = 8;
    constexpr int MAX_PLIES = 120;
    constexpr int NULL_MOVE_PERCENT = 10;

    std::mt19937 rng(12345);
    Board board;

    for (const std::string& fen : playout_fens()) {
        for (int playout = 0; playout < PLAYOUTS_PER_POSITION; playout++) {
            board.set_fen(fen);
            std::deque<BoardMemory> memories;
            std::vector<std::string> history;
            std::vector<bool> was_null;

            for (int ply = 0; ply < MAX_PLIES; ply++) {
                MoveList moves;
                moves.generate<ALL_LEGAL_MOVES>(board);
                if (moves.size() == 0)
                    break;

                for (Move m : moves) {
                    const std::string san = m.san_notation(board);
                    CHECK_EQ_CTX(Move::from_alg_notation(board, san).uci_notation(), m.uci_notation(), board.fen() + " " + san);
                }

                history.push_back(board.fen());
                memories.emplace_back();

                const bool null_move = !board.checkers() && !board.previous_was_null_move() &&
                                       int(rng() % 100) < NULL_MOVE_PERCENT;
                if (null_move)
                    board.make_null_move(memories.back());
                else
                    board.make_move(moves.at(rng() % moves.size()), memories.back());
                was_null.push_back(null_move);

                CHECK_EQ_CTX(board.hash(), board.make_full_hash(), board.fen());
                CHECK_CTX(accumulator_matches_refresh(board), board.fen());
            }

            while (!history.empty()) {
                if (was_null.back())
                    board.unmake_null_move();
                else
                    board.unmake_move();
                CHECK_EQ(board.fen(), history.back());
                CHECK_EQ(board.hash(), board.make_full_hash());
                CHECK_CTX(accumulator_matches_refresh(board), board.fen());
                history.pop_back();
                was_null.pop_back();
            }
        }
    }
}

TEST(san_notation_edge_cases)
{
    struct SanCase
    {
        const char* fen;
        const char* uci;
        const char* san;
    };
    const SanCase cases[] = {
        { "4k3/8/8/8/8/5N2/8/1N2K3 w - - 0 1", "b1d2", "Nbd2" },
        { "4k3/8/8/8/8/5N2/8/1N2K3 w - - 0 1", "f3d2", "Nfd2" },
        { "4k3/8/8/8/R7/8/8/R3K3 w - - 0 1", "a1a2", "R1a2" },
        { "4k3/8/8/8/R7/8/8/R3K3 w - - 0 1", "a4a2", "R4a2" },
        { "2k5/8/8/8/4Q2Q/8/8/K6Q w - - 0 1", "h4e1", "Qh4e1" },
        { "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", "e5d6", "exd6" },
        { "4k3/1P6/8/8/8/8/8/4K3 w - - 0 1", "b7b8q", "b8=Q+" },
        { "r3k3/1P6/8/8/8/8/8/4K3 w - - 0 1", "b7a8n", "bxa8=N" },
        { "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1g1", "O-O" },
        { "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1c1", "O-O-O" },
        { "r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", "e8g8", "O-O" },
        { "r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", "e8c8", "O-O-O" },
        { "6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1", "d1d8", "Rd8#" },
        { "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4", "c4f7", "Bxf7+" },
    };

    Board board;
    for (const SanCase& c : cases) {
        board.set_fen(c.fen);
        Move move = find_move(board, c.uci);
        CHECK_CTX(!move.is_none(), c.uci);
        if (move.is_none())
            continue;
        CHECK_EQ_CTX(move.san_notation(board), std::string(c.san), c.fen);
        CHECK_EQ_CTX(Move::from_alg_notation(board, c.san).uci_notation(), std::string(c.uci), c.fen);
    }
}

static void play(Board& board, std::deque<BoardMemory>& memories, const std::vector<std::string>& moves)
{
    for (const std::string& uci : moves) {
        memories.emplace_back();
        board.make_move(find_move(board, uci), memories.back());
    }
}

TEST(repetition_detection)
{
    Board board;
    std::deque<BoardMemory> memories;

    board.set_fen(START_FEN);
    play(board, memories, { "g1f3", "g8f6", "f3g1", "f6g8" });
    CHECK(board.treat_as_draw_by_repetition(10));
    CHECK(!board.treat_as_draw_by_repetition(0));
    CHECK(!board.is_draw_by_repetition());

    play(board, memories, { "g1f3", "g8f6", "f3g1", "f6g8" });
    CHECK(board.is_draw_by_repetition());
    CHECK(board.treat_as_draw_by_repetition(0));

    // A cycle due to null moves is not a repetition.
    board.set_fen(START_FEN);
    memories.clear();
    memories.emplace_back();
    board.make_move(find_move(board, "g1f3"), memories.back());
    memories.emplace_back();
    board.make_null_move(memories.back());
    memories.emplace_back();
    board.make_move(find_move(board, "f3g1"), memories.back());
    memories.emplace_back();
    board.make_null_move(memories.back());
    CHECK(!board.treat_as_draw_by_repetition(10));
}

TEST(en_passant_square_only_set_when_capture_is_legal)
{
    struct EpCase
    {
        const char* fen;
        const char* move;
        const char* expected_fen;
    };
    const EpCase cases[] = {
        // No pawn can capture.
        { START_FEN, "e2e4", "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1" },
        // Can capture.
        { "rnbqkbnr/ppp1pppp/8/8/3p4/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "e2e4",
          "rnbqkbnr/ppp1pppp/8/8/3pP3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1" },
        // Can capture, but is pinned
        { "8/8/8/8/R2p3k/8/4P3/4K3 w - - 0 1", "e2e4", "8/8/8/8/R2pP2k/8/8/4K3 b - - 0 1" },
    };

    Board board, expected;
    std::deque<BoardMemory> memories;
    for (const EpCase& c : cases) {
        board.set_fen(c.fen);
        play(board, memories, { c.move });
        CHECK_EQ_CTX(board.fen(), std::string(c.expected_fen), c.fen);
        expected.set_fen(c.expected_fen);
        CHECK_EQ_CTX(board.hash(), expected.hash(), c.fen); // Also checks the incremental hash.
    }

    // Drop EP in FEN if capture is illegal
    board.set_fen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
    CHECK_EQ(board.fen(), std::string("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1"));
}

TEST(fifty_move_rule)
{
    Board board;
    std::deque<BoardMemory> memories;

    board.set_fen("4k3/8/8/8/8/8/8/R3K3 w - - 99 80");
    CHECK(!board.is_draw_by_fifty_move());
    play(board, memories, { "a1a2" });
    CHECK(board.is_draw_by_fifty_move());

    board.set_fen("4k3/8/8/8/8/8/P7/4K3 w - - 99 80");
    play(board, memories, { "a2a3" }); // pawn move
    CHECK(!board.is_draw_by_fifty_move());
}

TEST(mirrored_fen)
{
    Board board;
    board.set_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w Kq - 0 1");
    CHECK_EQ(board.mirrored_fen(), std::string("r3k2r/pppbbppp/2n2q1P/1P2p3/3pn3/BN2PNP1/P1PPQPB1/R3K2R b Qk - 0 1"));

    board.set_fen("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1");
    CHECK_EQ(board.mirrored_fen(), std::string("4k3/8/8/8/3Pp3/8/8/4K3 b - d3 0 1"));

    for (const std::string& fen : playout_fens()) {
        board.set_fen(fen);
        const std::string canonical = board.fen();
        board.set_fen(board.mirrored_fen());
        CHECK_EQ(board.mirrored_fen(), canonical);
    }
}
