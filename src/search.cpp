#include "search.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "movegen.h"
#include "moveorder.h"
#include "rng.h"
#include "tune.h"
#include "util.h"

#ifdef SEARCH_STATS
#define STAT(counter) (stats.counter++)
#else
#define STAT(counter) ((void)0)
#endif

constexpr int QUIESCENCE_DEPTH = 0;

TUNABLE(NULL_MOVE_REDUCTION, 3, 1, 6);
TUNABLE(NULL_MOVE_DEPTH_DIVISOR, 7, 2, 12);
TUNABLE(NULL_MOVE_MIN_DEPTH, 1, 1, 8);

TUNABLE(DELTA_MARGIN, 313, 0, 500);

TUNABLE(LMR_MIN_DEPTH, 3, 1, 6);
TUNABLE(LMR_MIN_MOVE_INDEX, 4, 1, 8);
TUNABLE(LMR_BASE_PERCENT, 63, 0, 200);
TUNABLE(LMR_DIVISOR_PERCENT, 186, 100, 500);

TUNABLE(HISTORY_BONUS_DEPTH_SQUARED_PERCENT, 118, 25, 400);

TUNABLE(RFP_MAX_DEPTH, 7, 4, 12);
TUNABLE(RFP_MARGIN_FACTOR, 81, 25, 400);
TUNABLE(RFP_EVAL_WEIGHT_PERCENT, 47, 0, 100);

TUNABLE(ASPIRATION_WINDOW_SIZE, 49, 1, 200);
TUNABLE(ASPIRATION_WINDOW_GROWTH_PERCENT, 416, 150, 800);

static bool zugzwang_risk(const Board& board)
{
    return pop_count(board.occ(board.side()) & ~board.occ(board.side() * PAWN)) <= 1;
}

static bool is_draw(const Board& board, int root_dist)
{
    return board.is_draw_by_fifty_move() || board.is_insufficient_material() || board.treat_as_draw_by_repetition(root_dist);
}

static int late_move_reduction(int depth, int move_index)
{
    return int(LMR_BASE_PERCENT / 100.0 + std::log(depth) * std::log(move_index) / (LMR_DIVISOR_PERCENT / 100.0));
}

void SearchWorker::tt_store(TEntryHandle& handle, BoardEval eval, Move best_move, int depth, TableBound bound, int root_dist)
{
    master->ttable.insert(handle, eval_to_tt(eval, root_dist), best_move, depth, bound);
}

static BoardEval lazy_eval(const Board& board, BoardEval& eval)
{
    if (eval >= INF_EVAL)
        eval = evaluate(board);
    return eval;
}

template<SearchNode Node>
BoardEval SearchWorker::alpha_beta(Board& board, StackFrame& f, BoardEval alpha, BoardEval beta, int depth)
{
    assert(alpha < beta);
    assert(implies(Node != PV_NODE, beta - alpha == 1));
    assert(implies(f.is_leftmost, Node == PV_NODE));

    if (f.root_dist >= MAX_DEPTH)
        return evaluate(board);

    // Go into quiescence search when we reach the horizon.
    if (depth <= 0)
        return quiescence<Node>(board, f, alpha, beta);

    nodes_searched.store(nodes_searched.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    sel_depth = std::max(sel_depth, f.root_dist);

    BoardEval original_alpha = alpha;

    MoveList moves;
    moves.generate<ALL_LEGAL_MOVES>(board);

    // We check if we have reached the end of the match.
    if (moves.size() == 0)
        return board.checkers() ? -MATE_EVAL + f.root_dist : 0;
    if (f.root_dist > 0 && is_draw(board, f.root_dist))
        return 0;

    TTProbe tt_probe = master->ttable.get(board.hash());
    TEntryHandle& tt_handle = tt_probe.handle;
    tt_handle.eval = eval_from_tt(tt_handle.eval, f.root_dist);
    STAT(tt_probes);
    if (tt_probe.hit)
        STAT(tt_hits);

    // Check for TT cut off. Do not use transposition table in PV nodes.
    if (Node != PV_NODE && tt_probe.hit && tt_handle.depth >= depth) {
        if (tt_handle.bound == LOWER_BOUND)
            alpha = std::max(alpha, tt_handle.eval);
        else if (tt_handle.bound == UPPER_BOUND)
            beta = std::min(beta, tt_handle.eval);
        else {
            STAT(tt_cutoffs);
            return tt_handle.eval;
        }

        if (alpha >= beta) {
            STAT(tt_cutoffs);
            return alpha;
        }
    }

    // Prepare the next stack frame.
    StackFrame next_frame;
    next_frame.root_dist = f.root_dist + 1;
    next_frame.extensions = f.extensions;

    const bool is_in_check = board.checkers();

    if (Node != PV_NODE && !is_in_check) {
        BoardEval static_eval = INF_EVAL;

        // Reverse futility pruning
        if ((!tt_probe.hit || tt_handle.best_move.is_none() || board.is_capture(tt_probe.handle.best_move)) &&
            depth <= RFP_MAX_DEPTH &&
            !is_mate(beta) &&
            lazy_eval(board, static_eval) > beta + depth * RFP_MARGIN_FACTOR) {
            return (static_eval * RFP_EVAL_WEIGHT_PERCENT / 100 + beta * (100 - RFP_EVAL_WEIGHT_PERCENT) / 100);
        }

        // Try null move to see if it causes a beta cutoff.
        // Assuming making a move is better than not making one, if a beta cutoff occurs with a null
        // move, it will likely also occur in normal search. If no cutoff occurs, we search the node
        // as normal.
        if (depth >= NULL_MOVE_MIN_DEPTH &&
            !board.previous_was_null_move() &&
            !zugzwang_risk(board) &&
            lazy_eval(board, static_eval) >= beta) {

            assert(!f.is_leftmost);

            next_frame.pv.length = 0;
            next_frame.is_leftmost = false;

            STAT(null_tries);

            BoardMemory memory;
            board.make_null_move(memory);

            const int reduction = NULL_MOVE_REDUCTION + depth / NULL_MOVE_DEPTH_DIVISOR;
            BoardEval null_eval = -alpha_beta<NON_PV_NODE>(board, next_frame, -beta, -beta + 1, depth - reduction);

            board.unmake_null_move();

            // An aborted null move search returns a meaningless value
            if (master->is_aborted())
                return alpha;

            if (null_eval >= beta) {
                STAT(null_cutoffs);
                // A null move is not legal, so a mate found after it is not correct.
                if (is_mate(null_eval))
                    null_eval = beta;
                tt_store(tt_handle, null_eval, tt_handle.best_move, depth, LOWER_BOUND, f.root_dist);
                return null_eval;
            }
        }
    }

    // If we are on the PV of the previous iteration, we place the next move of the PV
    // first. We make sure that the last iterations PV is the first explored path.
    if (Node == PV_NODE && f.is_leftmost && f.root_dist < prev_pv.len())
        tt_handle.best_move = prev_pv.moves[f.root_dist];

    MovePicker picker(board, moves, tt_handle.best_move, killers[f.root_dist], history[board.side()]);

    BoardEval best_eval = -INF_EVAL;

    Move quiets_searched[MAX_MOVES];
    int quiets_searched_count = 0;

    const Move* killers_here = killers[f.root_dist];

    // Consider every legal move
    int i = 0;
    for (Move move = picker.next(); !move.is_none(); move = picker.next(), i++) {

        BoardEval eval;
        next_frame.pv.length = 0;

        // Read before move is made.
        const bool is_quiet = !board.is_loud(move);
        const bool is_killer = std::find(killers_here, killers_here + KILLER_SLOTS, move) != killers_here + KILLER_SLOTS;

        BoardMemory memory;
        board.make_move(move, memory);
        const bool gives_check = board.checkers(); // TODO detect check before making the move

        // Perform check extension
        int extension = 0;
        if (gives_check && f.extensions < iteration_depth) {
            board.unmake_move();
            // only checks that do not lose material
            extension = see_threshold(board, move, 0);
            board.make_move(move, memory);
        }
        next_frame.extensions = f.extensions + extension;

        // We assume that the first child will be part of the PV and we update our bounds based on
        // it. We assume that the other children are not as good and we search them with a null
        // window to increase the number of cutoffs. If they turn out to be good, we do the full
        // search.
        if (Node == PV_NODE && i == 0) {

            next_frame.is_leftmost = f.is_leftmost;

            eval = -alpha_beta<PV_NODE>(board, next_frame, -beta, -alpha, depth - 1 + extension);

        } else {
            next_frame.is_leftmost = false;

            int reduction = 0;
            if (depth >= LMR_MIN_DEPTH && i >= LMR_MIN_MOVE_INDEX && is_quiet && !is_killer && !is_in_check && !gives_check)
                reduction = std::clamp(late_move_reduction(depth, i), 0, depth - 2);

            eval = -alpha_beta<NON_PV_NODE>(board, next_frame, -alpha - 1, -alpha, depth - 1 + extension - reduction);

            // Verify at full depth.
            if (reduction > 0 && eval > alpha)
                eval = -alpha_beta<NON_PV_NODE>(board, next_frame, -alpha - 1, -alpha, depth - 1 + extension);

            // If the null window search can raise alpha, do the full search.
            if (Node == PV_NODE && eval > alpha && eval < beta)
                eval = -alpha_beta<PV_NODE>(board, next_frame, -beta, -alpha, depth - 1 + extension);
        }

        board.unmake_move();

        // Check if the stopping criteria have been reached. We check before updating alpha since we cannot
        // trust an aborted search which the evaluation is (likely) based on.
        if (check_for_stop())
            break;

        // Data collection for the transposition table. Only moves that raise alpha are known to be best.
        best_eval = std::max(best_eval, eval);
        if (eval > alpha)
            tt_handle.best_move = move;

        // Check for cut-off.
        if (eval >= beta) {
            STAT(beta_cutoffs);
            if (i == 0)
                STAT(first_move_cutoffs);
            if (std::find(killers[f.root_dist], killers[f.root_dist] + KILLER_SLOTS, move) != killers[f.root_dist] + KILLER_SLOTS)
                STAT(killer_cutoffs);

            // We assume captures ordered well, so only quiet moves are remembered.
            if (!board.is_loud(move)) {
                const MoveEval bonus = depth * depth * HISTORY_BONUS_DEPTH_SQUARED_PERCENT / 100;
                store_killer(f.root_dist, move);
                update_history(board.side(), move, bonus);
                for (int i = 0; i < quiets_searched_count; i++)
                    update_history(board.side(), quiets_searched[i], -bonus);
            }

            alpha = eval;
            break;
        }

        if (!board.is_loud(move))
            quiets_searched[quiets_searched_count++] = move;

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

    // Add table entry (unless the search was aborted and the result is therefore incomplete).
    if (!master->is_aborted()) {
        assert(best_eval > -INF_EVAL);
        TableBound bound = best_eval >= beta ? LOWER_BOUND : (best_eval <= original_alpha ? UPPER_BOUND : EXACT);
        assert(implies(bound == EXACT, Node == PV_NODE));
        tt_store(tt_handle, best_eval, tt_handle.best_move, depth, bound, f.root_dist);
    }

    return alpha;
}

template<SearchNode Node>
BoardEval SearchWorker::quiescence(Board& board, StackFrame& f, BoardEval alpha, BoardEval beta)
{
    assert(Node == PV_NODE || beta - alpha == 1);
    assert(alpha < beta);

    nodes_searched.store(nodes_searched.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    sel_depth = std::max(sel_depth, f.root_dist);
    STAT(qnodes);

    if (f.root_dist >= MAX_PV_LENGTH - 1)
        return evaluate(board);

    const bool in_check = board.checkers();

    MoveList moves;
    if (in_check) {
        moves.generate<ALL_LEGAL_MOVES>(board);
        if (moves.size() == 0)
            return -MATE_EVAL + f.root_dist;
    }
    if (is_draw(board, f.root_dist))
        return 0;

    // Using the TT in quiescence seems to not be beneficial, perhaps because the current evaluation
    // function is fast.

    BoardEval static_eval = -INF_EVAL;
    if (!in_check) {

        // We assume that doing something is better than doing nothing, so the current static
        // evaluation serves as our lower bound.
        static_eval = evaluate(board);

        if (static_eval >= beta) {
            return static_eval;
        } else if (alpha < static_eval) {
            alpha = static_eval;
        }

        moves.generate<LOUD_MOVES>(board);

        // We check if we've reached a quiet position.
        if (moves.size() == 0)
            return static_eval;
    }

    Move best_move = Move::make_none();

    // Place previous PV's move in front if this is a leftmost node.
    if (Node == PV_NODE && f.is_leftmost && f.root_dist < prev_pv.len())
        best_move = prev_pv.moves[f.root_dist];

    MovePicker picker(board, moves, best_move);

    // Prepare the next stack frame.
    StackFrame next_frame;
    next_frame.root_dist = f.root_dist + 1;
    next_frame.extensions = f.extensions;

    int searched = 0;
    for (Move move = picker.next(); !move.is_none(); move = picker.next()) {

        // Delta pruning
        if (!in_check && !see_threshold(board, move, std::max<MoveEval>(0, alpha - static_eval - DELTA_MARGIN)))
            continue;

        BoardEval eval;
        next_frame.pv.length = 0;

        BoardMemory memory;
        board.make_move(move, memory);

        // We assume that the first child is the best move and search the others with a null window.
        if (Node == PV_NODE && searched == 0) {
            searched++;

            next_frame.is_leftmost = f.is_leftmost;

            eval = -quiescence<PV_NODE>(board, next_frame, -beta, -alpha);

        } else {
            searched++;

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
    completed_depth = 0;
    completed_sel_depth = 0;
    stats = SearchStats();
    for (Move* k : killers)
        std::fill(k, k + KILLER_SLOTS, Move::make_none());

    // Don't clear the history completely
    for (MoveEval* side_history : history)
        for (int i = 0; i < SQ_MAX * SQ_MAX; i++)
            side_history[i] /= 2;

    StackFrame root_frame;
    root_frame.is_leftmost = true;
    root_frame.root_dist = 0;
    root_frame.extensions = 0;
    root_frame.pv.length = 0;

    BoardEval last_best = -INF_EVAL;

    MoveList root_moves;
    root_moves.generate<ALL_LEGAL_MOVES>(board);

    // Start the iterative deepening loop
    int depth = 0;
    do {
        depth++;
        iteration_depth = depth;

        const Us iteration_start = now_us();
        sel_depth = 0;

        int window = ASPIRATION_WINDOW_SIZE;
        BoardEval alpha = -INF_EVAL, beta = INF_EVAL;
        if (depth > 1 && !is_mate(last_best)) {
            alpha = std::max<int>(last_best - window, -INF_EVAL);
            beta = std::min<int>(last_best + window, INF_EVAL);
        }
        while (true) {
            eval = alpha_beta<PV_NODE>(board, root_frame, alpha, beta, depth);
            if (master->is_aborted() || (eval > alpha && eval < beta))
                break;
            window = window * ASPIRATION_WINDOW_GROWTH_PERCENT / 100;
            if (is_mate(eval)) {
                alpha = -INF_EVAL;
                beta = INF_EVAL;
            } else if (eval <= alpha) {
                alpha = std::max<int>(eval - window, -INF_EVAL);
            } else if (eval >= beta) {
                beta = std::min<int>(eval + window, INF_EVAL);
            } else {
                break;
            }
        }
        const bool completed = !master->is_aborted();

        // Since we always search the previous PV first we can still use a partial search. In
        // the case of a partial search, we either find the previous PV is best, we find
        // out another path of nodes is actually better and use it, or we never get a proper
        // evaluation and we use the previous PV. By checking if the evaluation was infinity,
        // we check whether or not we got to do any searching that yielded results before
        // stopping.
        if (std::abs(eval) < INF_EVAL && eval > alpha && eval < beta) {
            last_best = eval;
            prev_pv.copy(root_frame.pv);
        }

        if (completed && master->is_main_worker(this)) {
            completed_depth = depth;
            completed_sel_depth = sel_depth;

            SearchResult res(INFO_RESULT);
            res.pv_line.copy(prev_pv);
            res.depth_searched = depth;
            res.sel_depth_reached = sel_depth;
            res.eval = last_best;
            res.nodes_searched = master->nodes_searched();
            res.time_ms = master->elapsed();

            receiver.receive_search_result(res);

#ifdef SEARCH_STATS
            log_sync(stats.to_string() + "\n");
#endif

            if (prev_pv.length > 0) {
                const IterationInfo iteration = { depth, prev_pv.moves[0], last_best, root_moves.size(), now_us() - iteration_start };
                if (master->should_stop_at_iteration(iteration))
                    master->stop();
            }

            if (master->mate_limit && is_mate(last_best) && last_best > 0 && MATE_EVAL - last_best <= 2 * master->mate_limit - 1)
                master->stop();
        }

    } while (depth < max_depth && !master->is_aborted());

    // If there is no PV, pick via move ordering
    if (prev_pv.length == 0 && root_moves.size() > 0) {
        prev_pv.moves[0] = MovePicker(board, root_moves, Move::make_none()).next();
        prev_pv.length = 1;
    }

    return last_best;
}

void SearchMaster::go(ISearchReceiver& receiver, Board& board, const SearchConditions& conditions)
{
    assert(worker_count > 0);

    wait_for();

    assert(!in_search);

    abort_search = false;
    in_search = true;
    is_ponder = conditions.ponder;
    stop_on_ponderhit = false;
    node_limit = conditions.nodes;
    mate_limit = conditions.mate_in;

    assert(conditions.depth > 0 && conditions.depth <= MAX_DEPTH);

    // Clock of zero is valid meaning no time left.
    const bool untimed = conditions.move_time == 0 && !conditions.clock_given;
    is_untimed = untimed;

    if (!untimed)
        time_manager.init(board.side(), conditions, move_overhead);

    ttable.next_age();

    start_time = now();

    search_thread = std::thread([&, conditions] { start_search(receiver, board, conditions); });
}

void SearchMaster::start_search(ISearchReceiver& receiver, Board& board, const SearchConditions& conditions)
{
    BoardEval eval = workers[0].iterative_deepening(receiver, board, conditions.depth);

    // Wait for stop in these modes, even if we are done
    while ((conditions.infinite || is_ponder) && !is_aborted())
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    SearchResult res(BEST_RESULT);
    res.pv_line.copy(workers[0].prev_pv);
    res.depth_searched = workers[0].completed_depth;
    res.sel_depth_reached = workers[0].completed_sel_depth;
    res.eval = eval;
    res.nodes_searched = nodes_searched();
    res.time_ms = elapsed();

    in_search = false;

    receiver.receive_search_result(res);
}

bool SearchWorker::check_for_stop()
{
    if (master->is_aborted()) {
        return true;

    } else if (master->is_main_worker(this)) {
        const uint64_t nodes = nodes_searched.load(std::memory_order_relaxed);

        if (master->node_limit && nodes >= master->node_limit) {
            master->stop();
            return true;
        }
        if (nodes << 55 == 0)
            master->check_time();
    }

    return false;
}

void SearchMaster::check_time()
{
    if (!is_ponder.load(std::memory_order_acquire) &&
        !is_untimed.load(std::memory_order_relaxed) &&
        time_manager.hard_limit_reached())
        stop();
}

bool SearchMaster::should_stop_at_iteration(const IterationInfo& iteration)
{
    if (is_untimed.load(std::memory_order_relaxed))
        return false;

    const bool pondering = is_ponder.load(std::memory_order_acquire);

    // Also called while pondering to record info.
    const bool stop = time_manager.should_stop_at_iteration(iteration, time_manager.elapsed_us());
    if (pondering) {
        stop_on_ponderhit = stop;
        // A ponderhit may have read the flag before this write, so we check again (SC load).
        if (!is_ponder)
            return stop;
    }
    return stop && !pondering;
}

void SearchMaster::realise_ponder()
{
    assert(is_ponder && in_search);

    time_manager.start();
    is_ponder = false;

    if (stop_on_ponderhit)
        stop();
}

void SearchWorker::clear_history()
{
    memset(history, 0, sizeof(history));
}

void SearchMaster::clear_history()
{
    for (int i = 0; i < worker_count; i++)
        workers[i].clear_history();
}

SearchWorker::SearchWorker()
    : iteration_depth(0)
    , completed_depth(0)
    , sel_depth(0)
    , completed_sel_depth(0)
    , nodes_searched(0)
{
    clear_history();
}

SearchMaster::SearchMaster(size_t worker_count, size_t ttable_size)
    : ttable(TTable(ttable_size))
    , node_limit(0)
    , mate_limit(0)
    , move_overhead(DEFAULT_MOVE_OVERHEAD_MS)
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

    // Zero means we pick a worker count.
    if (count == 0)
        count = std::clamp<size_t>(std::thread::hardware_concurrency(), 1, MAX_WORKERS);
    worker_count = count;

    workers.reset(new SearchWorker[worker_count]);
    for (int i = 0; i < worker_count; i++)
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
    nodes_searched = time_ms = eval = depth_searched = sel_depth_reached = 0;
    pv_line.set_empty();
}

void SearchWorker::store_killer(int dist, Move move)
{
    Move* slots = killers[dist];
    for (size_t i = std::find(slots, slots + KILLER_SLOTS - 1, move) - slots; i > 0; i--)
        slots[i] = slots[i - 1];
    slots[0] = move;
}

void SearchWorker::update_history(Colour side, Move move, MoveEval bonus)
{
    MoveEval clamped_bonus = std::clamp(bonus, -MAX_HISTORY_BONUS, MAX_HISTORY_BONUS);
    history[side][move.from_to_index()] += clamped_bonus - history[side][move.from_to_index()] * std::abs(clamped_bonus) / MAX_HISTORY;
}

std::string SearchStats::to_string() const
{
    auto percent = [](uint64_t part, uint64_t whole) { return std::to_string(whole ? 100 * part / whole : 0) + "%"; };
    return "info string stats tt_probes " + std::to_string(tt_probes) +
           " tt_hits " + percent(tt_hits, tt_probes) +
           " tt_cutoffs " + percent(tt_cutoffs, tt_probes) +
           " null_tries " + std::to_string(null_tries) +
           " null_cutoffs " + percent(null_cutoffs, null_tries) +
           " qnodes " + std::to_string(qnodes) +
           " beta_cutoffs " + std::to_string(beta_cutoffs) +
           " first_move_cutoffs " + percent(first_move_cutoffs, beta_cutoffs) +
           " killer_cutoffs " + percent(killer_cutoffs, beta_cutoffs);
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