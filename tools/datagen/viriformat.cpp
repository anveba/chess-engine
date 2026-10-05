#include "viriformat.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "board.h"
#include "search.h"

constexpr uint8_t PIECE_CODE[MAX_PIECE_TYPE] = { 0, 0, 3, 1, 2, 4, 5 };
constexpr uint8_t PROMOTION_CODE[MAX_PIECE_TYPE] = { 0, 0, 2, 0, 1, 3, 0 };
constexpr uint8_t UNMOVED_ROOK_CODE = 6;
constexpr int16_t MATE_SCORE = 32767;
constexpr size_t HEADER_SIZE = 32;
constexpr uint32_t END_OF_GAME = 0;

template<typename T>
static void put(std::vector<uint8_t>& out, T value)
{
    for (size_t i = 0; i < sizeof(T); i++)
        out.push_back(uint8_t(value >> (8 * i)));
}

static void put_position(std::vector<uint8_t>& out, const Board& board, GameResult result)
{
    Bitboard castling_rooks = 0;
    for (CastlingRights cr : { CASTLING_W_KINGSIDE, CASTLING_W_QUEENSIDE, CASTLING_B_KINGSIDE, CASTLING_B_QUEENSIDE })
        if (board.has_castling_right(cr))
            castling_rooks |= bb_set(rook_castle_from(cr));

    uint8_t pieces[16] = {};
    Bitboard occupied = board.occ();
    for (int i = 0; occupied; i++) {
        const Square sq = pop_lsb(occupied);
        const Piece piece = board.at(sq);
        const uint8_t type = (castling_rooks & bb_set(sq)) ? UNMOVED_ROOK_CODE : PIECE_CODE[type_of(piece)];
        pieces[i / 2] |= (type | (colour_of(piece) == BLACK ? 8 : 0)) << (4 * (i % 2));
    }

    uint8_t ep_square = 64;
    if (board.ep_rights() != NO_EP_RIGHTS)
        ep_square = file_of(board.ep_rights()) + 8 * (board.side() == WHITE ? 5 : 2);

    put<uint64_t>(out, board.occ());
    out.insert(out.end(), pieces, pieces + 16);
    put<uint8_t>(out, (board.side() == BLACK ? 0x80 : 0) | ep_square);
    put<uint8_t>(out, std::min<uint32_t>(board.fifty_move_counter(), 255));
    put<uint16_t>(out, board.current_fullmove());
    put<int16_t>(out, 0); // Unused.
    put<uint8_t>(out, result);
    put<uint8_t>(out, 0); // Unused.
}

static uint16_t encode_move(Move move)
{
    const uint16_t squares = move.from_sq() | (move.to_sq() << 6);
    if (move.is_ep())
        return squares | (1 << 14);
    if (move.is_castle())
        return squares | (2 << 14);
    if (move.is_promotion())
        return squares | (3 << 14) | (PROMOTION_CODE[move.promotion_to()] << 12);
    return squares;
}

void GameRecord::serialize_viri(std::ostream& out) const
{
    Board board;
    board.set_fen(start_fen);

    std::vector<uint8_t> bytes;
    put_position(bytes, board, result);
    for (size_t i = 0; i < moves.size(); i++) {
        const int score = white_scores[i];
        put(bytes, encode_move(moves[i]));
        put<int16_t>(bytes, is_mate(BoardEval(score)) ? (score > 0 ? MATE_SCORE : -MATE_SCORE) : score);
    }
    put<uint32_t>(bytes, END_OF_GAME);
    out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

static bool read_past_complete_game(std::istream& in)
{
    char header[HEADER_SIZE];
    if (!in.read(header, sizeof(header)))
        return false;

    uint32_t move_and_score;
    while (in.read(reinterpret_cast<char*>(&move_and_score), sizeof(move_and_score)))
        if (move_and_score == END_OF_GAME)
            return true;
    return false;
}

int64_t keep_complete_viri_games(const std::string& path)
{
    if (!std::filesystem::exists(path))
        return 0;

    std::ifstream in(path, std::ios::binary);
    int64_t complete_games = 0;
    std::streamoff end_of_complete_games = 0;
    while (read_past_complete_game(in)) {
        complete_games++;
        end_of_complete_games = in.tellg();
    }
    in.close();

    std::filesystem::resize_file(path, end_of_complete_games);
    return complete_games;
}
