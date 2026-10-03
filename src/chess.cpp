#include "chess.h"

#include "board.h"
#include "movegen.h"

#include <algorithm>
#include <sstream>

std::string Move::uci_notation() const
{
    if (is_none())
        return "none";
    else if (is_null())
        return "0000";

    std::ostringstream ss;
    ss << char('a' + file_of(from_sq())) << char('1' + rank_of(from_sq()));
    if (is_castle()) {
        ss << (to_sq() == SQ_A1   ? "c1"
               : to_sq() == SQ_H1 ? "g1"
               : to_sq() == SQ_A8 ? "c8"
               : to_sq() == SQ_H8 ? "g8"
                                  : "");
    } else
        ss << char('a' + file_of(to_sq())) << char('1' + rank_of(to_sq()));
    if (is_promotion()) {
        ss << (promotion_to() == KNIGHT   ? "n"
               : promotion_to() == ROOK   ? "r"
               : promotion_to() == BISHOP ? "b"
               : promotion_to() == QUEEN  ? "q"
                                          : "");
    }
    return ss.str();
}

std::string Move::san_notation(Board& board) const
{
    assert(is_legal(board));

    const Board& b = board;

    std::string san;
    const Piece piece = b.at(from_sq());
    const bool capture = is_ep() || (is_piece(b.at(to_sq())) && !is_castle());
    const std::string to_str = { char('a' + file_of(to_sq())), char('1' + rank_of(to_sq())) };

    if (is_castle()) {
        san = file_of(to_sq()) == file_of(SQ_H1) ? "O-O" : "O-O-O";
    } else if (type_of(piece) == PAWN) {
        if (capture)
            san += std::string(1, char('a' + file_of(from_sq()))) + "x";
        san += to_str;
        if (is_promotion())
            san += std::string("=") + to_fen_code(WHITE * promotion_to());
    } else {
        san += to_fen_code(WHITE * type_of(piece));

        // Check for ambiguity
        MoveList moves;
        moves.generate<ALL_LEGAL_MOVES>(board);
        bool ambiguous = false, same_file = false, same_rank = false;
        for (Move m : moves) {
            if (m.to_sq() != to_sq() || m.from_sq() == from_sq() || b.at(m.from_sq()) != piece || m.is_castle())
                continue;
            ambiguous = true;
            same_file |= file_of(m.from_sq()) == file_of(from_sq());
            same_rank |= rank_of(m.from_sq()) == rank_of(from_sq());
        }

        // Clear the ambiguity
        if (ambiguous && (!same_file || same_rank))
            san += char('a' + file_of(from_sq()));
        if (ambiguous && same_file)
            san += char('1' + rank_of(from_sq()));

        if (capture)
            san += "x";
        san += to_str;
    }

    BoardMemory memory;
    board.make_move(*this, memory);
    if (board.checkers()) {
        MoveList replies;
        replies.generate<ALL_LEGAL_MOVES>(board);
        san += replies.size() == 0 ? "#" : "+";
    }
    board.unmake_move();

    return san;
}

Move Move::from_alg_notation(const Board& board, const std::string& notation)
{
    const Colour c = board.side();

    // Check for castle moves
    std::string stripped = notation;
    for (char ch : { '+', '#' })
        stripped.erase(std::remove(stripped.begin(), stripped.end(), ch), stripped.end());

    if (stripped == "O-O" || stripped == "0-0")
        return Move::make_castle(square_wrt(c, SQ_E1), square_wrt(c, SQ_H1));

    if (stripped == "O-O-O" || stripped == "0-0-0")
        return Move::make_castle(square_wrt(c, SQ_E1), square_wrt(c, SQ_A1));

    Square from_sq = SQ_NONE, to_sq = SQ_NONE;
    MoveType type = NORMAL_MOVE;
    PieceType promotion_to = NO_PIECE_TYPE;

    std::string n = notation;

    // Remove characters that are optional or do not add any meaning to the move itself.
    for (char ch : { '+', '=', '#' })
        n.erase(std::remove(n.begin(), n.end(), ch), n.end());

    if (isupper(n[0])) {
        // Non-pawn move

        n.erase(std::remove(n.begin(), n.end(), 'x'), n.end());

        to_sq = square_of(n[n.size() - 1] - '1', n[n.size() - 2] - 'a');

        // Get the file and/or rank of the moved piece that make the move unambiguous (if they are given).
        // A negative value means none was given.
        int from_file = -1, from_rank = -1;
        for (size_t i = 1; i + 2 < n.size(); i++) {
            char d = n[i];
            if (d >= 'a' && d <= 'h')
                from_file = d - 'a';
            else if (d >= '1' && d <= '8')
                from_rank = d - '1';
            else
                assert(0);
        }

        Piece piece = type_of(fen_code_to_piece(n[0])) * c;
        Bitboard piece_bb = board.occ(piece);

        from_sq = SQ_NONE;

        // Loop through all pieces of the given type and see which one fits the move
        while (piece_bb) {
            Square sq = pop_lsb(piece_bb);

            // Filter pieces that don't fit the given file/rank.
            Bitboard sq_bb = bb_set(sq);
            if (from_file >= 0)
                sq_bb &= FILE_A << from_file;
            if (from_rank >= 0)
                sq_bb &= RANK_1 << from_rank * BOARD_LEN;

            if (!sq_bb)
                continue;

            // Check if the considered piece can move to the square in question.
            MoveList moves;
            moves.generate<ALL_LEGAL_MOVES>(board);
            for (Move m : moves) {
                if (m.from_sq() == sq && to_sq == m.to_sq() && !m.is_castle()) {
                    from_sq = m.from_sq();
                    break;
                }
            }

            if (from_sq != SQ_NONE)
                break;
        }

    } else {
        // Pawn move

        // Handle promotion
        if (isupper(n[n.size() - 1])) {
            promotion_to = type_of(fen_code_to_piece(n[n.size() - 1]));
            type = PROMOTION_MOVE;

            n.erase(n.end() - 1, n.end());
        }

        to_sq = square_of(n[n.size() - 1] - '1', n[n.size() - 2] - 'a');

        if (notation[1] == 'x') {
            // Move is a pawn capture

            int from_file = notation[0] - 'a';
            Bitboard capture_bb = pawn_capture_mask_sq(~c, to_sq);
            Bitboard from_bb = capture_bb & (FILE_A << from_file);

            assert(from_bb);
            from_sq = lsb_idx(from_bb);

            // Handle EP
            if (board.ep_rights() != NO_EP_RIGHTS &&
                from_bb & (c == WHITE ? RANK_5 : RANK_4) &&
                file_of(board.ep_rights()) == file_of(to_sq)) {

                assert(type == NORMAL_MOVE);
                type = EP_MOVE;
            }
        } else {
            // Move is a pawn push

            // Check if it was a normal or double push
            Bitboard move_bb = pawn_move_mask_bb(~c, bb_set(to_sq));
            if (board.occ(c * PAWN) & move_bb)
                from_sq = lsb_idx(move_bb);
            else if (board.occ(c * PAWN) & pawn_move_mask_bb(~c, move_bb))
                from_sq = lsb_idx(pawn_move_mask_bb(~c, move_bb));
            else
                assert(0);
        }
    }

    assert(from_sq != SQ_NONE);
    assert(to_sq != SQ_NONE);

    Move move;
    if (type == NORMAL_MOVE)
        move = Move::make_normal(from_sq, to_sq);
    else if (type == EP_MOVE)
        move = Move::make_ep(from_sq, to_sq);
    else
        move = Move::make_promotion(from_sq, to_sq, promotion_to);

    assert(move.is_legal(board));
    return move;
}

bool Move::is_legal(const Board& board) const
{
    MoveList moves;
    moves.generate<ALL_LEGAL_MOVES>(board);

    for (Move move : moves)
        if (move == *this)
            return true;

    return false;
}

Move Move::from_uci_notation(const Board& board, const std::string& notation)
{
    if (notation == "none" || notation == "(none)")
        return Move::make_none();
    if (notation == "null" || notation == "(null)")
        return Move::make_null();
    PieceType promotion_to = NO_PIECE_TYPE;
    MoveType move_type = NORMAL_MOVE;

    Square from_sq = square_of(notation[1] - '1', notation[0] - 'a');
    Square to_sq = square_of(notation[3] - '1', notation[2] - 'a');

    Piece moved_piece = board.at(from_sq);
    Piece captured_piece = board.at(to_sq);

    // Handle promotion
    if (notation.size() == 5) {
        assert(type_of(moved_piece) == PAWN);
        promotion_to = type_of(fen_code_to_piece(notation[4]));
        move_type = PROMOTION_MOVE;
    }

    // Handle EP
    if (type_of(moved_piece) == PAWN &&
        captured_piece == NO_PIECE &&
        file_of(from_sq) != file_of(to_sq)) {

        move_type = EP_MOVE;
    }

    // Handle castling
    if (type_of(moved_piece) == KING &&
        std::abs(file_of(from_sq) - file_of(to_sq)) > 1) {

        move_type = CASTLING_MOVE;
        Colour c = colour_of(moved_piece);

        // Moves are encoded as the king moving to the rook's square, so we have to change the
        // square moved to.
        assert(to_sq == square_wrt(c, SQ_C1) || to_sq == square_wrt(c, SQ_G1));
        to_sq = to_sq == square_wrt(c, SQ_C1) ? square_wrt(c, SQ_A1)
                                              : square_wrt(c, SQ_H1);
    }

    Move move = Move::make_none();
    if (move_type == NORMAL_MOVE)
        move = Move::make_normal(from_sq, to_sq);
    else if (move_type == EP_MOVE)
        move = Move::make_ep(from_sq, to_sq);
    else if (move_type == CASTLING_MOVE)
        move = Move::make_castle(from_sq, to_sq);
    else if (move_type == PROMOTION_MOVE)
        move = Move::make_promotion(from_sq, to_sq, promotion_to);

    assert(move.is_legal(board));

    return move;
}

Piece fen_code_to_piece(char code)
{
    switch (code) {
        case 'r':
            return B_ROOK;
        case 'n':
            return B_KNIGHT;
        case 'b':
            return B_BISHOP;
        case 'q':
            return B_QUEEN;
        case 'k':
            return B_KING;
        case 'p':
            return B_PAWN;
        case 'R':
            return W_ROOK;
        case 'N':
            return W_KNIGHT;
        case 'B':
            return W_BISHOP;
        case 'Q':
            return W_QUEEN;
        case 'K':
            return W_KING;
        case 'P':
            return W_PAWN;
        default:
            return NO_PIECE;
    }
}

char to_fen_code(Piece p)
{
    char c;
    switch (type_of(p)) {
        case PAWN:
            c = 'P';
            break;
        case ROOK:
            c = 'R';
            break;
        case KNIGHT:
            c = 'N';
            break;
        case BISHOP:
            c = 'B';
            break;
        case QUEEN:
            c = 'Q';
            break;
        case KING:
            c = 'K';
            break;
        default:
            c = '.';
            break;
    }
    if (colour_of(p) == BLACK)
        c += 32;
    return c;
}