#include "search.h"

#include <cmath>
#include <cstring>

#include "movegen.h"
#include "rng.h"
#include "util.h"

constexpr int NULL_MOVE_REDUCTION = 3;
constexpr int NULL_MOVE_MIN_DEPTH = 4;

static_assert(NULL_MOVE_REDUCTION < NULL_MOVE_MIN_DEPTH);

static void put_first(Move* start, Move* end, Move new_first)
{
    for (Move* move = start; move != end; move++) {
        if (*move == new_first) {
            std::swap(*start, *move);
            return;
        }
    }
    assert(0);
}

static bool zugzwang_risk(const Board& board)
{
    return pop_count(board.occ(board.side()) & ~board.occ(board.side() * PAWN)) <= 2;
}

template<SearchNode Node>
BoardEval SearchWorker::alpha_beta(Board& board, StackFrame& f, BoardEval alpha, BoardEval beta, int depth)
{
    assert(iff(Node == PV_NODE, beta - alpha > 1));
    assert(alpha < beta);

    // Go into quiescence when we reach the horizon.
    if (depth <= 0)
        return quiescence<Node>(board, alpha, beta);

    // Check if the stopping criteria have been reached. We do not check for PV nodes
    // as we would like to make sure we return something reasonable in the case of
    // premature stopping. This is made more important due to the fact that we check
    // the last iteration's PV first.
    if (Node == NON_PV_NODE && master->should_stop())
        return 0;

    // Prepare the next stack frame.
    StackFrame next_frame;
    BoardEval eval;
    next_frame.root_dist = f.root_dist + 1;

    // Try null move to see if it causes a beta cutoff.
    // Assuming making a move is better than not making one, if a beta cutoff occurs with a null
    // move, it will likely also occur in normal search. If no cutoff occurs, we search the node
    // as normal.
    if (!f.is_leftmost &&
        !board.previous_was_null_move() &&
        depth >= NULL_MOVE_MIN_DEPTH &&
        !board.checkers() &&
        !zugzwang_risk(board)) {

        assert(!f.is_leftmost);

        next_frame.pv.length = 0;
        next_frame.is_leftmost = false;

        BoardMemory memory;
        board.make_null_move(memory);

        constexpr SearchNode next_node = Node == PV_NODE ? PV_NODE : NON_PV_NODE;

        BoardEval null_eval = -alpha_beta<next_node>(board, next_frame, -beta, -alpha, depth - NULL_MOVE_REDUCTION);

        board.unmake_null_move();

        if (null_eval >= beta)
            return null_eval;
    }

    MoveList moves;
    moves.generate<ALL_LEGAL_MOVES>(board);

    // We check if we have reached the end of the match.
    if (moves.size() == 0)
        return board.checkers() ? -MATE_EVAL : 0;

    sort_moves(board, moves.begin(), moves.end());

    // If we are on the PV of the previous iteration, we place the next move of the PV
    // first. We make sure that the last iterations PV is the first explored path.
    if (Node == PV_NODE && f.is_leftmost && f.root_dist < prev_pv.len())
        put_first(moves.begin(), moves.end(), prev_pv.moves[f.root_dist]);

    // Consider every legal move
    for (Move move : moves) {

        next_frame.pv.length = 0;

        BoardMemory memory;
        board.make_move(move, memory);

        // We assume that the first child will be part of the PV and we update our bounds based on
        // it. We assume that the other children are not as good and we search them with a null
        // window to increase the number of cutoffs. If they turn out to be good, we do the full
        // search.
        if (Node == PV_NODE && move == moves.at(0)) {

            next_frame.is_leftmost = f.is_leftmost;

            eval = -alpha_beta<PV_NODE>(board, next_frame, -beta, -alpha, depth - 1);

        } else {
            next_frame.is_leftmost = false;

            eval = -alpha_beta<NON_PV_NODE>(board, next_frame, -alpha - 1, -alpha, depth - 1);

            // If the null window search can raise alpha, do the full search.
            if (alpha < eval && eval < beta) {
                constexpr SearchNode next_node = Node == PV_NODE ? PV_NODE : NON_PV_NODE;

                eval = -alpha_beta<next_node>(board, next_frame, -beta, -alpha, depth - 1);
            }
        }

        board.unmake_move();

        // Update alpha and the PV (if we are in a PV node).
        if (eval > alpha) {
            alpha = eval;

            if (Node == PV_NODE) {
                f.pv.moves[0] = move;
                std::memcpy(f.pv.moves + 1, next_frame.pv.moves, next_frame.pv.length * sizeof(Move));
                f.pv.length = next_frame.pv.length + 1;
            }
        }

        if (alpha >= beta)
            break;
    }

    return alpha;
}

template<SearchNode Node>
BoardEval SearchWorker::quiescence(Board& board, BoardEval alpha, BoardEval beta)
{
    assert(iff(Node == PV_NODE, beta - alpha > 1));
    assert(alpha < beta);

    if (Node == NON_PV_NODE && master->should_stop())
        return 0;

    MoveList moves;
    moves.generate<ALL_LEGAL_MOVES>(board);

    // We check if we have reached the end of the match.
    if (moves.size() == 0)
        return board.checkers() ? -MATE_EVAL : 0;

    // We assume that doing something is better than doing nothing, so the current static
    // evaluation serves as our lower bound.
    const BoardEval static_eval = evaluate(board);

    if (static_eval >= beta)
        return static_eval;
    else if (alpha < static_eval)
        alpha = static_eval;

    moves.filter_quiet(board);

    // We check if we've reached a quiet position.
    if (moves.size() == 0)
        return static_eval;

    sort_moves(board, moves.begin(), moves.end());

    BoardEval eval;

    for (Move move : moves) {

        BoardMemory memory;
        board.make_move(move, memory);

        // We assume that the first child is the best move and search the others with a null window.
        if (Node == PV_NODE && move == moves.at(0)) {

            eval = -quiescence<PV_NODE>(board, -beta, -alpha);

        } else {

            eval = -quiescence<NON_PV_NODE>(board, -alpha - 1, -alpha);

            // If the null window search failed, do the full search.
            if (alpha < eval && eval < beta) {
                constexpr SearchNode next_node = Node == PV_NODE ? PV_NODE : NON_PV_NODE;

                eval = -quiescence<next_node>(board, -beta, -alpha);
            }
        }

        board.unmake_move();

        if (eval > alpha)
            alpha = eval;

        if (alpha >= beta)
            break;
    }

    return alpha;
}

BoardEval SearchWorker::iterative_deepening(ISearchReceiver& receiver, Board& board, int max_depth)
{
    BoardEval eval;
    prev_pv.length = 0;

    // Start the iterative deepening loop
    int depth = 0;
    do {
        depth++;

        StackFrame root_frame;
        root_frame.is_leftmost = true;
        root_frame.root_dist = 0;
        root_frame.pv.length = 0;

        eval = alpha_beta<PV_NODE>(board, root_frame, -INF_EVAL, INF_EVAL, depth);

        // Since we always search the previous PV first we can still use a partial search. In
        // the case of a partial search, we either use the last best move anyway, or we find
        // out another path of nodes is actually better and use it.
        prev_pv.copy(root_frame.pv);

        if (master->is_main_worker(this) && !master->should_stop())
            receiver.receive_search_result(SearchResult(CURRENT_BEST, prev_pv, eval));

    } while (depth < max_depth && !master->should_stop());

    return eval;
}

void SearchMaster::go(ISearchReceiver& receiver, Board& board, int depth, float time, bool ponder)
{
    assert(depth > 0 && depth <= MAX_DEPTH);
    assert(worker_count > 0);
    assert(!in_search);

    wait_for();

    abort_search = false;
    in_search = true;
    is_ponder = ponder;

    search_thread = std::thread([&, depth] { start_search(receiver, board, depth); });

    timer_thread = std::thread([&, time] { start_timer(time); });
}

void SearchMaster::start_search(ISearchReceiver& receiver, Board& board, int depth)
{
    BoardEval eval = workers[0].iterative_deepening(receiver, board, depth);

    in_search = false;

    receiver.receive_search_result(SearchResult(FINAL_BEST, workers[0].prev_pv, eval));
}

void SearchMaster::start_timer(float time)
{
    constexpr float wake_inc = 0.005f;
    const float earliness = 0.05f;
    float elapsed;

    auto start = std::chrono::system_clock::now();

    do {
        std::this_thread::sleep_for(std::chrono::milliseconds(uint64_t(wake_inc * 1000.0f)));

        auto now = std::chrono::system_clock::now();

        if (is_ponder) {
            start = now;
            elapsed = 0.0f;
        } else {
            elapsed = std::chrono::duration_cast<std::chrono::duration<float>>(now - start).count();
        }

    } while (in_search && elapsed + wake_inc + earliness < time);

    stop();
}

void SearchMaster::realise_ponder()
{
    assert(is_ponder && in_search);
    is_ponder = false;
}

SearchMaster::SearchMaster(int worker_count)
    : in_search(false)
    , search_receiver(nullptr)
{
    set_workers(worker_count);
}

SearchMaster::~SearchMaster()
{
    wait_for();
}

void SearchMaster::set_workers(int count)
{
    assert(count > 0);
    assert(!in_search);

    worker_count = count;

    for (int i = 0; i < count; i++)
        workers[i].master = this;
}

void SearchMaster::wait_for()
{
    if (search_thread.joinable())
        search_thread.join();
    if (timer_thread.joinable())
        timer_thread.join();
}

std::string PVLine::to_string() const
{
    std::string str;
    for (int i = 0; i < length; i++)
        str += moves[i].uci_notation() + " ";
    return str;
}

void PVLine::copy(const PVLine& pv)
{
    length = pv.length;
    std::memcpy(moves, pv.moves, pv.length * sizeof(Move));
}