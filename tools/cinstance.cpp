#include "cinstance.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <poll.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <wait.h>

#include "board.h"

static void pipe_with_check(int* pipe_ends)
{
    if (pipe(pipe_ends) == -1) {
        std::cerr << "pipe error: " << strerror(errno) << std::endl;
        exit(1);
    }
}

static void dup2_with_check(int fd1, int fd2)
{
    if (dup2(fd1, fd2) == -1) {
        std::cerr << "dup2 error: " << strerror(errno) << std::endl;
        exit(1);
    }
}

ChessInstance::ChessInstance(const std::string& name, const std::string& executable_path)
    : name(name)
    , is_searching(false)
    , buffer_start(sizeof(read_buffer))
    , buffer_end(sizeof(read_buffer))
{
    assert(!name.empty());

    int stdin_pipe[2], stdout_pipe[2];
    pipe_with_check(stdin_pipe);
    pipe_with_check(stdout_pipe);

    pid = fork();
    if (pid == 0) {
        prctl(PR_SET_PDEATHSIG, SIGHUP);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        dup2_with_check(stdin_pipe[0], STDIN_FILENO);
        dup2_with_check(stdout_pipe[1], STDOUT_FILENO);
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        execl(executable_path.c_str(), executable_path.c_str(), NULL);
        std::cerr << "exec error: " << strerror(errno) << std::endl;
        exit(1);
    }

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);

    fd_in = stdin_pipe[1];
    fd_out = stdout_pipe[0];
}

ChessInstance::~ChessInstance()
{
    if (is_running()) {
        if (is_searching)
            write_to("stop\n");
        write_to("quit\n");
        waitpid(pid, NULL, 0);
    }
    close(fd_in);
    close(fd_out);
}

bool ChessInstance::is_running() const
{
    return !(kill(pid, 0) == -1 && errno == ESRCH);
}

void ChessInstance::set_option(const std::string name, const std::string value)
{
    write_to("setoption name " + name + " value " + value + "\n");
}

void ChessInstance::set_board(const std::string& fen, const std::vector<Move>& moves)
{
    std::string moves_str;
    if (!moves.empty()) {
        moves_str = " moves";
        for (const Move& m : moves)
            moves_str += " " + m.uci_notation();
    }

    write_to("position fen " + fen + moves_str + "\n");
}

bool ChessInstance::wait_for_ready()
{
    write_to("isready\n");
    while (read_token() != "readyok") {
    }
    return true;
}

void ChessInstance::indicate_new_game()
{
    write_to("ucinewgame\n");
}

void ChessInstance::start_search()
{
    write_to("go\n");
    is_searching = true;
}

void ChessInstance::get_search_result(const Board& board, InstanceMoveResponse& result)
{
    if (!is_searching) {
        std::cerr << "Error: Tried to get result of search that hasn't started." << std::endl;
        exit(1);
    }
    write_to("stop\n");
    is_searching = false;

    result.evaluation = 0;
    result.mate_in = false;
    result.depth = -1;

    std::string token;
    while ((token = read_token()) != "bestmove") {

        if (token == "score") {
            std::string score_type = read_token();
            result.evaluation = std::stoi(read_token());
            if (score_type == "mate") {
                result.mate_in = true;
            } else if (score_type == "cp") {
            } else {
                std::cerr << "Unknown score type";
            }
        } else if (token == "depth") {
            result.depth = std::stoi(read_token());
        }
    }
    result.move = Move::from_uci_notation(board, read_token());
}

static bool is_delimiter(char c)
{
    return c == ' ' || c == '\n';
}

static size_t first_non_delimiter(const char* str, size_t start, size_t end)
{
    for (size_t i = start; i < end; i++)
        if (!is_delimiter(str[i]))
            return i;
    return end;
}

static size_t first_delimiter(const char* str, size_t start, size_t end)
{
    for (size_t i = start; i < end; i++)
        if (is_delimiter(str[i]))
            return i;
    return end;
}

std::string ChessInstance::read_token()
{
    std::string result;
    while (true) {
        size_t delim_pos = first_delimiter(read_buffer, buffer_start, buffer_end);

        if (delim_pos < buffer_end) {
            result += std::string(read_buffer + buffer_start, delim_pos - buffer_start);
            buffer_start = first_non_delimiter(read_buffer, delim_pos, buffer_end);
            if (!result.empty())
                break;
        } else {
            result += std::string(read_buffer + buffer_start, buffer_end - buffer_start);
            buffer_start = 0;
            buffer_end = read(fd_out, read_buffer, sizeof(read_buffer));
        }
    }
    assert(first_delimiter(result.c_str(), 0, strlen(result.c_str())) == result.size());
    return result;
}

void ChessInstance::write_to(const std::string& msg)
{
    if (write(fd_in, msg.c_str(), strlen(msg.c_str())) == -1)
        std::cerr << "write error" << std::endl;
}