#ifndef CINSTANCE_H_INCLUDED
#define CINSTANCE_H_INCLUDED

#include <cstdlib>
#include <vector>

#include "chess.h"
#include "eval.h"

struct InstanceMoveResponse
{
    Move move;
    BoardEval evaluation;
    bool mate_in;
    int depth;
};

struct ChessOption
{
    std::string name;
    std::string value;
};

class ChessInstance
{
  public:
    ChessInstance(const std::string& name, const std::string& executable_path);
    ~ChessInstance();

    const std::string& get_name() const { return name; };
    bool is_running() const;

    void set_option(const std::string name, const std::string value);
    void set_board(const std::string& fen, const std::vector<Move>& moves);

    bool wait_for_ready();
    void indicate_new_game();
    void start_search();
    void get_search_result(const Board& board, InstanceMoveResponse& result);

    ChessInstance(const ChessInstance&) = delete;
    ChessInstance& operator=(const ChessInstance&) = delete;

  private:
    std::string name;
    int pid, fd_in, fd_out;
    bool is_searching;

    char read_buffer[2048];
    size_t buffer_start;
    size_t buffer_end;

    std::string read_token();
    void write_to(const std::string& msg);
};

#endif