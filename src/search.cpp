#include "search.h"

#include <cmath>
#include <cstring>

#include "movegen.h"
#include "rng.h"
#include "util.h"

constexpr int QUIESCENCE_DEPTH = 0;

constexpr int NULL_MOVE_REDUCTION = 3;
constexpr int NULL_MOVE_MIN_DEPTH = 4;

static_assert(NULL_MOVE_REDUCTION < NULL_MOVE_MIN_DEPTH);

// Swaps the given move with the first move in the list. If the move is not in the list,
// nothing will happen.
static void swap_with_first(Move* start, Move* end, Move new_first)
{
    for (Move* move = start; move != end; move++) {
        if (*move == new_first) {
            std::swap(*start, *move);
            return;
        }
    }
}

static bool zugzwang_risk(const Board& board)
{
    return pop_count(board.occ(board.side()) & ~board.occ(board.side() * PAWN)) <= 2;
}

template<SearchNode Node>
BoardEval SearchWorker::alpha_beta(Board& board, StackFrame& f, BoardEval alpha, BoardEval beta, int depth)
{
    assert(alpha < beta);
    assert(implies(Node != PV_NODE, beta - alpha == 1));
    assert(implies(f.is_leftmost, Node == PV_NODE));

    // Go into quiescence search when we reach the horizon.
    if (depth <= 0)
        return quiescence<Node>(board, f, alpha, beta);

    nodes_searched.store(nodes_searched.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);

    BoardEval original_alpha = alpha;

    MoveList moves;
    moves.generate<ALL_LEGAL_MOVES>(board);

    // We check if we have reached the end of the match.
    if (moves.size() == 0)
        return board.checkers() ? -MATE_EVAL + f.root_dist : 0;
    if (board.is_draw_by_fifty_move() || board.treat_as_draw_by_repetition(f.root_dist))
        return 0;

    auto [tt_hit, tt_handle] = master->ttable.get(board.hash());

    // Check for TT cut off. Do not use transposition table in PV nodes.
    if (Node != PV_NODE && tt_hit && tt_handle.depth >= depth) {
        if (tt_handle.bound == LOWER_BOUND)
            alpha = std::max(alpha, tt_handle.eval);
        else if (tt_handle.bound == UPPER_BOUND)
            beta = std::min(beta, tt_handle.eval);
        else
            return tt_handle.eval;

        if (alpha >= beta)
            return alpha;
    }

    // Prepare the next stack frame.
    StackFrame next_frame;
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

        if (null_eval >= beta) {
            master->ttable.insert(tt_handle, null_eval, tt_handle.best_move, depth, LOWER_BOUND);
            return null_eval;
        }
    }

    // If we are on the PV of the previous iteration, we place the next move of the PV
    // first. We make sure that the last iterations PV is the first explored path.
    if (Node == PV_NODE && f.is_leftmost && f.root_dist < prev_pv.len())
        tt_handle.best_move = prev_pv.moves[f.root_dist];

    // Sort moves after swapping moves around in the move list. If a move has been placed first, it
    // is skipped when sorting.
    if (tt_handle.best_move.is_proper())
        swap_with_first(moves.begin(), moves.end(), tt_handle.best_move);
    sort_moves(board, moves.begin() + size_t(tt_handle.best_move.is_proper()), moves.end());

    BoardEval best_eval = -INF_EVAL;

    // Consider every legal move
    for (Move move : moves) {

        BoardEval eval;
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
            if (Node == PV_NODE && eval > alpha && eval < beta)
                eval = -alpha_beta<PV_NODE>(board, next_frame, -beta, -alpha, depth - 1);
        }

        board.unmake_move();

        // Check if the stopping criteria have been reached. We check before updating alpha since we cannot
        // trust an aborted search which the evaluation is (likely) based on.
        if (check_for_stop())
            break;

        // Data collection for the transposition table.
        if (eval >= best_eval) {
            best_eval = eval;
            tt_handle.best_move = move;
        }

        // Check for cut-off.
        if (eval >= beta) {
            alpha = eval;
            break;
        }

        // Update alpha and the PV (if we are in a PV node).
        if (eval > alpha) {
            alpha = eval;

            if (Node == PV_NODE) {
                f.pv.moves[0] = move;
                std::memcpy(f.pv.moves + 1, next_frame.pv.moves, next_frame.pv.length * sizeof(Move));
                f.pv.length = next_frame.pv.length + 1;
            }
        }
    }

    // Add table entry. The evaluation may be -infinity if we have aborted in which case we
    // don't want to store results from an incomplete search.
    if (best_eval > -INF_EVAL) {
        TableBound bound = best_eval >= beta ? LOWER_BOUND : (best_eval <= original_alpha ? UPPER_BOUND : EXACT);
        assert(implies(bound == EXACT, Node == PV_NODE));
        master->ttable.insert(tt_handle, best_eval, tt_handle.best_move, depth, bound);
    }

    return alpha;
}

template<SearchNode Node>
BoardEval SearchWorker::quiescence(Board& board, StackFrame& f, BoardEval alpha, BoardEval beta)
{
    assert(Node == PV_NODE || beta - alpha == 1);
    assert(alpha < beta);

    nodes_searched.store(nodes_searched.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);

    MoveList moves;
    moves.generate<ALL_LEGAL_MOVES>(board);

    // We check if we have reached the end of the match.
    if (moves.size() == 0)
        return board.checkers() ? -MATE_EVAL + f.root_dist : 0;
    if (board.is_draw_by_fifty_move() || board.treat_as_draw_by_repetition(f.root_dist))
        return 0;

    // Using the TT in quiescence seems to not be beneficial, perhaps because the current evaluation
    // function is fast.

    // We assume that doing something is better than doing nothing, so the current static
    // evaluation serves as our lower bound.
    const BoardEval static_eval = evaluate(board);

    if (static_eval >= beta) {
        return static_eval;
    } else if (alpha < static_eval) {
        alpha = static_eval;
    }

    moves.filter_quiet(board);

    // We check if we've reached a quiet position.
    if (moves.size() == 0)
        return static_eval;

    Move best_move = Move::make_none();

    // Place previous PV's move in front if this is a leftmost node.
    if (Node == PV_NODE && f.is_leftmost && f.root_dist < prev_pv.len())
        best_move = prev_pv.moves[f.root_dist];

    if (best_move.is_proper())
        swap_with_first(moves.begin(), moves.end(), best_move);

    sort_moves(board, moves.begin() + size_t(best_move.is_proper()), moves.end());

    // Prepare the next stack frame.
    StackFrame next_frame;
    next_frame.root_dist = f.root_dist + 1;

    for (Move move : moves) {

        BoardEval eval;
        next_frame.pv.length = 0;

        BoardMemory memory;
        board.make_move(move, memory);

        // We assume that the first child is the best move and search the others with a null window.
        if (Node == PV_NODE && move == moves.at(0)) {

            next_frame.is_leftmost = f.is_leftmost;

            eval = -quiescence<PV_NODE>(board, next_frame, -beta, -alpha);

        } else {
            next_frame.is_leftmost = false;

            eval = -quiescence<NON_PV_NODE>(board, next_frame, -alpha - 1, -alpha);

            // If the null window search failed, do the full search.
            if (Node == PV_NODE && eval > alpha && eval < beta)
                eval = -quiescence<PV_NODE>(board, next_frame, -beta, -alpha);
        }

        board.unmake_move();

        if (check_for_stop())
            break;

        if (eval >= beta) {
            alpha = eval;
            break;
        }

        if (eval > alpha) {

            alpha = eval;

            if (Node == PV_NODE) {
                f.pv.moves[0] = move;
                std::memcpy(f.pv.moves + 1, next_frame.pv.moves, next_frame.pv.length * sizeof(Move));
                f.pv.length = next_frame.pv.length + 1;
            }
        }
    }

    return alpha;
}

BoardEval SearchWorker::iterative_deepening(ISearchReceiver& receiver, Board& board, int max_depth)
{
    BoardEval eval;
    prev_pv.length = 0;
    nodes_searched = 0;

    StackFrame root_frame;
    root_frame.is_leftmost = true;
    root_frame.root_dist = 0;
    root_frame.pv.length = 0;

    BoardEval last_best = -INF_EVAL;

    // Start the iterative deepening loop
    int depth = 0;
    do {
        depth++;

        eval = alpha_beta<PV_NODE>(board, root_frame, -INF_EVAL, INF_EVAL, depth);

        // std::cerr << "TT diagnostics (depth " << depth
        //           << "):\n  hits      " << master->ttable.hits
        //           << "\n  misses    " << master->ttable.misses
        //           << "\n  rate      " << (float)master->ttable.hits / master->ttable.misses
        //           << "\n  occupancy " << master->ttable.occupancy()
        //           << "\n  slots     " << master->ttable.cluster_count() * CLUSTER_SIZE << std::endl;

        // Since we always search the previous PV first we can still use a partial search. In
        // the case of a partial search, we either find the previous PV is best, we find
        // out another path of nodes is actually better and use it, or we never get a proper
        // evaluation and we use the previous PV. By checking if the evaluation was infinity,
        // we check whether or not we got to do any searching that yielded results before
        // stopping.
        if (std::abs(eval) < INF_EVAL) {
            last_best = eval;
            prev_pv.copy(root_frame.pv);
        }

        if (master->is_main_worker(this)) {

            SearchResult res(INFO_RESULT);
            res.pv_line.copy(prev_pv);
            res.depth_searched = depth;
            res.eval = last_best;
            res.nodes_searched = master->nodes_searched();
            res.time_ms = master->elapsed();

            receiver.receive_search_result(res);
        }

    } while (depth < max_depth && !master->is_aborted());

    return last_best;
}

void SearchMaster::go(ISearchReceiver& receiver, Board& board, const SearchConditions& conditions)
{
    assert(worker_count > 0);

    wait_for();

    search_thread = std::thread([&, conditions] { start_search(receiver, board, conditions); });
}

void SearchMaster::start_search(ISearchReceiver& receiver, Board& board, const SearchConditions& conditions)
{
    assert(!in_search);

    abort_search = false;
    in_search = true;
    is_ponder = conditions.ponder;

    assert(conditions.depth > 0 && conditions.depth <= MAX_DEPTH);

    bool inf = conditions.move_time == 0 && (conditions.wtime == 0 || conditions.btime == 0);
    is_infinite = inf;

    if (!inf) {

        time_manager.set(board.side(), conditions);

        if (!conditions.ponder)
            time_manager.start();
    }

    ttable.next_age();

    start_time = now();

    BoardEval eval = workers[0].iterative_deepening(receiver, board, conditions.depth);

    in_search = false;

    SearchResult res(BEST_RESULT);
    res.pv_line.copy(workers[0].prev_pv);
    res.depth_searched = conditions.depth;
    res.eval = eval;
    res.nodes_searched = nodes_searched();
    res.time_ms = elapsed();

    receiver.receive_search_result(res);
}

bool SearchWorker::check_for_stop()
{
    if (master->is_aborted()) {
        return true;

    } else if (master->is_main_worker(this) &&
               nodes_searched.load(std::memory_order_relaxed) << 55 == 0) {

        master->check_time();
    }

    return false;
}

void SearchMaster::check_time()
{
    if (!is_ponder.load(std::memory_order_relaxed) &&
        !is_infinite.load(std::memory_order_relaxed) &&
        time_manager.check())
        stop();
}

void SearchMaster::realise_ponder()
{
    assert(is_ponder && in_search);
    is_ponder = false;
    time_manager.start();
}

SearchWorker::SearchWorker()
    : nodes_searched(0)
{
}

SearchMaster::SearchMaster(size_t worker_count, size_t ttable_size)
    : ttable(TTable(ttable_size))
    , in_search(false)
    , search_receiver(nullptr)
{
    set_worker_count(worker_count);
}

SearchMaster::~SearchMaster()
{
    wait_for();
}

void SearchMaster::set_worker_count(size_t count)
{
    assert(!is_searching());
    if (count == 0)
        worker_count = std::thread::hardware_concurrency();
    else
        worker_count = count;

    for (size_t i = 0; i < count; i++)
        workers[i].master = this;
}

uint64_t SearchMaster::nodes_searched() const
{
    uint64_t total = 0;

    for (int i = 0; i < worker_count; i++)
        total += workers[i].nodes_searched.load(std::memory_order_relaxed);

    return total;
}

void SearchMaster::wait_for()
{
    if (search_thread.joinable())
        search_thread.join();
}

SearchResult::SearchResult(SearchResultType type)
    : result_type(type)
{
    nodes_searched = time_ms = eval = depth_searched = 0;
    pv_line.set_empty();
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