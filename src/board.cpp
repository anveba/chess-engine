#include "board.h"

#include <cassert>
#include <sstream>

#include "rng.h"
#include "util.h"

uint64_t piece_sq_key[MAX_PIECE][SQ_MAX];
uint64_t black_to_move_key;
uint64_t castling_rights_key[MAX_CASTLE];
uint64_t ep_rights_key[MAX_EP];

void precompute_zobrist()
{
    Splitmix64 sm(1);
    Xshiro256 rng(sm.next(), sm.next(), sm.next(), sm.next());

    for (int i = 0; i < MAX_PIECE; i++)
        for (Square sq = SQ_ZERO; sq < SQ_MAX; sq++)
            piece_sq_key[i][sq] = i == NO_PIECE ? 0 : rng.next();

    black_to_move_key = rng.next();

    for (int i = 0; i < MAX_CASTLE; i++)
        castling_rights_key[i] = rng.next();

    for (int i = 0; i < MAX_EP; i++)
        ep_rights_key[i] = rng.next();
}

void BoardMemory::operator>>(BoardMemory& to)
{
    to.castling_rights = castling_rights;
    to.ep_rights = NO_EP_RIGHTS;
    to.fifty_move_counter = fifty_move_counter;
    to.hash = hash;

    to.previous = this;
}

void Board::recalculate_transients() const
{
    Square king_sq = lsb_idx(occ(side() * KING));
    head->checkers = threats_to<true>(side(), king_sq, occ());

    Bitboard potential_threats =
        (piece_move_mask<ROOK>(king_sq) & (occ(ROOK * ~side()) | occ(QUEEN * ~side()))) |
        (piece_move_mask<BISHOP>(king_sq) & (occ(BISHOP * ~side()) | occ(QUEEN * ~side())));

    head->pinned = BB_EMPTY;
    while (potential_threats) {
        Square threat_sq = pop_lsb(potential_threats);
        Bitboard threat_line = line_between(king_sq, threat_sq);

        Bitboard sandwiched_pieces = threat_line & occ();

        if (exactly_one(sandwiched_pieces) && (sandwiched_pieces & occ(side())))
            head->pinned |= sandwiched_pieces & occ(side());
    }
}

void Board::make_move(Move move, BoardMemory& memory)
{
    assert(move.is_proper());

    *head >> memory;
    head = &memory;
    memory.move = move;

    Piece moved_piece = at(move.from_sq());

    if (move.is_promotion()) {
        memory.captured = at(move.to_sq());

        remove_piece(move.from_sq());
        remove_piece(move.to_sq());
        place_piece(move.to_sq(), move.promotion_to() * side());

        head->castling_rights -= relevant_castle[move.to_sq()];

        memory.fifty_move_counter = 0;

    } else if (move.is_castle()) {
        memory.captured = NO_PIECE;
        bool queen_side = move.from_sq() > move.to_sq();

        Square king_to = square_wrt(side(), queen_side ? SQ_C1 : SQ_G1);
        Square rook_to = square_wrt(side(), queen_side ? SQ_D1 : SQ_F1);

        remove_piece(move.from_sq());
        remove_piece(move.to_sq());
        place_piece(king_to, KING * side());
        place_piece(rook_to, ROOK * side());

        head->castling_rights -= relevant_castle[move.from_sq()];

        memory.fifty_move_counter++;

    } else if (move.is_ep()) {
        memory.captured = PAWN * ~side();

        move_piece(move.from_sq(), move.to_sq());
        remove_piece(sq_move(move.to_sq(), side() == WHITE ? SOUTH : NORTH));

        memory.fifty_move_counter = 0;

    } else {
        memory.captured = move_piece(move.from_sq(), move.to_sq());

        if (type_of(moved_piece) == PAWN && std::abs(rank_of(move.from_sq()) - rank_of(move.to_sq())) > 1)
            head->ep_rights = file_to_ep(file_of(move.from_sq()));

        head->castling_rights -= relevant_castle[move.from_sq()];
        head->castling_rights -= relevant_castle[move.to_sq()];

        if (is_piece(memory.captured) || type_of(moved_piece) == PAWN)
            memory.fifty_move_counter = 0;
        else
            memory.fifty_move_counter++;
    }

    // Increment fullmove counter when black has moved
    fullmove_counter += side_to_move;

    side_to_move = ~side_to_move;
    head->hash ^= black_to_move_key;

    recalculate_transients();
}

void Board::unmake_move()
{
    assert(head->move.is_proper());

    Move move = head->move;

    side_to_move = ~side_to_move;

    // Decrement fullmove counter when black has unmoved
    fullmove_counter -= side_to_move;

    if (move.is_promotion()) {

        remove_piece(move.to_sq());
        if (is_piece(head->captured))
            place_piece(move.to_sq(), head->captured);
        place_piece(move.from_sq(), PAWN * side());

    } else if (move.is_castle()) {
        bool queen_side = move.from_sq() > move.to_sq();

        Square king_to = square_wrt(side(), queen_side ? SQ_C1 : SQ_G1);
        Square rook_to = square_wrt(side(), queen_side ? SQ_D1 : SQ_F1);

        place_piece(move.from_sq(), KING * side());
        place_piece(move.to_sq(), ROOK * side());
        remove_piece(king_to);
        remove_piece(rook_to);

    } else if (move.is_ep()) {

        move_piece(move.to_sq(), move.from_sq());
        place_piece(sq_move(move.to_sq(), side() == WHITE ? SOUTH : NORTH), PAWN * ~side());

    } else {
        unmove_piece(move.from_sq(), move.to_sq(), head->captured);
    }

    head = head->previous;
}

Piece Board::move_piece(Square from, Square to)
{
    const Piece moved = at(from);
    const Piece captured = at(to);

    assert(is_piece(moved));
    assert(type_of(captured) != KING);

    at(from) = NO_PIECE;
    at(to) = moved;

    const Bitboard from_bb = bb_set(from);
    const Bitboard to_bb = bb_set(to);
    const Bitboard from_to_bb = from_bb | to_bb;

    occupancy ^= from_to_bb;
    colour_occupancy[colour_of(moved)] ^= from_to_bb;
    piece_occupancy[moved] ^= from_to_bb;

    if (is_piece(captured)) {
        colour_occupancy[~colour_of(moved)] ^= to_bb;
        piece_occupancy[captured] ^= to_bb;
        occupancy |= to_bb;
    }

    return captured;
}

void Board::unmove_piece(Square from, Square to, Piece captured)
{
    const Piece moved = at(to);

    assert(is_piece(moved));
    assert(type_of(captured) != KING);

    at(from) = moved;
    at(to) = captured;

    const Bitboard from_bb = bb_set(from);
    const Bitboard to_bb = bb_set(to);
    const Bitboard from_to_bb = from_bb | to_bb;

    occupancy ^= from_to_bb;
    colour_occupancy[colour_of(moved)] ^= from_to_bb;
    piece_occupancy[moved] ^= from_to_bb;

    if (is_piece(captured)) {
        occupancy |= to_bb;
        colour_occupancy[~colour_of(moved)] |= to_bb;
        piece_occupancy[captured] |= to_bb;
    }
}

void Board::place_piece(Square sq, Piece piece)
{
    assert(is_piece(piece) && at(sq) == NO_PIECE);
    board[sq] = piece;
    occupancy |= bb_set(sq);
    colour_occupancy[colour_of(piece)] |= bb_set(sq);
    piece_occupancy[piece] |= bb_set(sq);
}

Piece Board::remove_piece(Square sq)
{
    Piece removed = board[sq];
    board[sq] = NO_PIECE;
    occupancy &= ~bb_set(sq);
    colour_occupancy[colour_of(removed)] &= ~bb_set(sq);
    piece_occupancy[removed] &= ~bb_set(sq);
    return removed;
}

void Board::make_null_move(BoardMemory& memory)
{
    assert(!checkers());

    *head >> memory;
    head = &memory;
    memory.move = Move::make_null();

    head->ep_rights = NO_EP_RIGHTS;

    head->fifty_move_counter++;

    fullmove_counter += side_to_move;

    side_to_move = ~side_to_move;
    head->hash ^= black_to_move_key;

    recalculate_transients();
}

void Board::unmake_null_move()
{
    assert(head->move.is_null());

    side_to_move = ~side_to_move;
    head->hash ^= black_to_move_key;

    fullmove_counter -= side_to_move;

    head = head->previous;
}

Board::Board()
{
    clear();

    for (Square sq = SQ_ZERO; sq < SQ_MAX; sq++)
        relevant_castle[sq] = NO_CASTLING_RIGHTS;

    relevant_castle[SQ_A1] = CASTLING_W_QUEENSIDE;
    relevant_castle[SQ_H1] = CASTLING_W_KINGSIDE;
    relevant_castle[SQ_A8] = CASTLING_B_QUEENSIDE;
    relevant_castle[SQ_H8] = CASTLING_B_KINGSIDE;
    relevant_castle[SQ_E1] = CASTLING_W;
    relevant_castle[SQ_E8] = CASTLING_B;
}

std::string Board::fen() const
{
    std::ostringstream ss;

    // Piece placement
    int empty_sqs = 0;
    for (int r = BOARD_LEN - 1; r >= 0; r--) {
        for (int f = 0; f < BOARD_LEN; f++) {
            Piece p = at(square_of(r, f));
            if (is_piece(p)) {
                ss << (empty_sqs > 0 ? std::to_string(empty_sqs) : "") << to_fen_code(p);
                empty_sqs = 0;
            } else
                empty_sqs++;
        }
        ss << (empty_sqs > 0 ? std::to_string(empty_sqs) : "")
           << (r > 0 ? "/" : "");
        empty_sqs = 0;
    }

    // Side to move
    ss << " " << std::string(side() == WHITE ? "w" : "b");

    // Castling rights
    ss << " ";
    if (castling_rights() == NO_CASTLING_RIGHTS)
        ss << "-";
    else
        ss << (has_castling_right(CASTLING_W_KINGSIDE) ? "K" : "")
           << (has_castling_right(CASTLING_W_QUEENSIDE) ? "Q" : "")
           << (has_castling_right(CASTLING_B_KINGSIDE) ? "k" : "")
           << (has_castling_right(CASTLING_B_QUEENSIDE) ? "q" : "");

    // EP rights
    ss << " ";
    if (ep_rights() == NO_EP_RIGHTS)
        ss << "-";
    else
        ss << char('a' + file_of(ep_rights())) << (side() == WHITE ? "6" : "3");

    // Fifty move counter
    ss << " " << fifty_move_counter();

    // Total move count
    ss << " " << current_fullmove();

    return ss.str();
}

void Board::set_fen(const std::string& fen)
{
    clear();

    std::istringstream ss(fen);
    std::string section;
    ss >> section;

    // Piece placement
    Square sq = SQ_A8;
    for (char c : section) {
        if (c >= '1' && c <= '8') {
            sq = sq_move(sq, EAST, c - '0');
        } else if (c == '/') {
            assert(sq % 8 == 0);
            sq = sq_move(sq, SOUTH, 2);
        } else {
            place_piece(sq, fen_code_to_piece(c));
            sq = sq_move(sq, EAST);
        }
    }

    // Side to move
    ss >> section;
    assert(section == "w" || section == "b");
    side_to_move = section == "w" ? WHITE : BLACK;

    // Castling rights
    ss >> section;
    for (char c : section) {
        if (c == 'K')
            head->castling_rights += CASTLING_W_KINGSIDE;
        else if (c == 'Q')
            head->castling_rights += CASTLING_W_QUEENSIDE;
        else if (c == 'k')
            head->castling_rights += CASTLING_B_KINGSIDE;
        else if (c == 'q')
            head->castling_rights += CASTLING_B_QUEENSIDE;
        else
            assert(c == '-');
    }

    // EP rights
    ss >> section;
    assert(section == "-" || section.size() == 2);
    if (section != "-") {
        assert(section[0] >= 'a' && section[0] <= 'h');
        assert(section[1] == '3' || section[1] == '6');
        assert(implies(section[1] == '3', side() == BLACK));
        assert(implies(section[1] == '6', side() == WHITE));
        head->ep_rights = file_to_ep(section[0] - 'a');
    }

    // Fifty move counter
    if (!(ss >> head->fifty_move_counter))
        head->fifty_move_counter = 0;

    // Fullmove counter
    if (!(ss >> fullmove_counter))
        fullmove_counter = 1;

    recalculate_transients();

    head->hash = make_full_hash();

    assert(is_valid());
}

bool Board::is_valid() const
{
    // Check rook and king positions are correct when castling is available.
    for (Colour c : { WHITE, BLACK }) {
        if (!implies(castling_rights() & (CASTLING_ALL & c), at(square_wrt(c, SQ_E1)) == c * KING))
            return false;
        for (CastlingRights cr : { CASTLING_QUEENSIDE, CASTLING_KINGSIDE })
            if (!implies(castling_rights() & (c & cr), at(rook_castle_from(c & cr)) == c * ROOK))
                return false;
    }

    // Check pawn is properly placed when EP is available.
    if (ep_rights() != NO_EP_RIGHTS &&
        !((FILE_A << file_of(ep_rights())) &
          (side() == WHITE ? RANK_5 : RANK_4) &
          occ(~side() * PAWN))) {
        return false;
    }

    // TODO: check bitboards

    // Check if pawns are before the relative second rank.
    if (occ(WHITE * PAWN) & RANK_1 || occ(BLACK * PAWN) & RANK_8)
        return false;

    // Check hash (TODO: hashing in make/unmake_move)
    // if (head->hash != make_full_hash())
    //    return false;

    // Check if there is exactly one of each king and if the other colour's king
    // is in check.
    return pop_count(occ(WHITE * KING)) == 1 &&
           pop_count(occ(BLACK * KING)) == 1 &&
           !threats_to<true>(~side(), lsb_idx(occ(~side() * KING)), occ());
}

BoardHash Board::make_full_hash() const
{
    BoardHash hash = side() == BLACK ? black_to_move_key : 0;

    for (Square sq = SQ_ZERO; sq < SQ_MAX; sq++)
        hash ^= piece_sq_key[at(sq)][sq];

    hash ^= castling_rights_key[head->castling_rights];
    hash ^= ep_rights_key[head->ep_rights];

    return hash;
}

void Board::clear()
{
    for (Square sq = SQ_ZERO; sq < SQ_MAX; sq++)
        board[sq] = NO_PIECE;

    occupancy = BB_EMPTY;
    colour_occupancy[WHITE] = colour_occupancy[BLACK] = BB_EMPTY;
    for (int i = 0; i < MAX_PIECE; i++)
        piece_occupancy[i] = BB_EMPTY;

    side_to_move = WHITE; // Arbitrary choice
    fullmove_counter = 0;

    root = BoardMemory();
    root.move = Move::make_none();
    root.captured = NO_PIECE;
    root.castling_rights = NO_CASTLING_RIGHTS;
    root.ep_rights = NO_EP_RIGHTS;
    root.fifty_move_counter = 0;
    root.pinned = root.checkers = BB_EMPTY;
    root.hash = 0;
    root.previous = nullptr;
    head = &root;
}

std::string Board::as_image_str() const
{
    std::ostringstream ss;
    ss << "+-----------------+\n";
    for (int r = BOARD_LEN - 1; r >= 0; r--) {
        ss << "| ";
        for (int f = 0; f < BOARD_LEN; f++) {
            ss << to_fen_code(at(square_of(r, f))) << " ";
        }
        ss << "| " << r + 1 << "\n";
    }
    ss << "+-----------------+\n";
    ss << "  a b c d e f g h  \n";
    ss << "Active colour: " << std::string(side_to_move == WHITE ? "White" : "Black") << "\n";
    ss << "FEN: " << fen() << "\n";
    ss << "Hash: " << head->hash;

    return ss.str();
}