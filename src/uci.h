#ifndef UCI_H_INCLUDED
#define UCI_H_INCLUDED

#include <deque>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "board.h"
#include "search.h"
#include "timestrat.h"

class UCISearchReceiver : public ISearchReceiver
{
  public:
    void receive_search_result(const SearchResult& result) override;
};

enum OptionType
{
    STRING_OPTION,
    SPIN_OPTION,
    CHECK_OPTION,
};

class UCIOption
{
  public:
    UCIOption(const std::string& name, OptionType type, const std::string& default_value);
    ~UCIOption();

    const std::string& get_name() const { return name; }
    const OptionType get_type() const { return type; }

    void set_on_change_callback(std::function<void(UCIOption*)> on_change) { this->on_change = on_change; }

    bool set(const std::string& value);
    bool set_int_bounds(int64_t min, int64_t max);

    const std::string& get_string() const;
    uint64_t get_int() const;
    bool get_bool() const;

    std::string uci_info() const;

  private:
    std::string name;
    OptionType type;

    std::function<void(UCIOption*)> on_change;

    std::string string_value;

    int64_t spin_value;
    int64_t spin_min, spin_max;

    bool check_value;
};

class UCI
{
  public:
    UCI();
    void start();

  private:
    void position(std::istringstream& in);
    void go(std::istringstream& in);
    void set_option(std::istringstream& in);
    void new_game();
    void start_perft(std::istringstream& in);
    void start_test(std::istringstream& in);
    void start_bench(std::istringstream& in);
    std::string legal_moves_str();
    std::string move_order_str();

  private:
    std::vector<UCIOption> options;

    std::deque<BoardMemory> board_memories;
    Board board;

    SearchMaster searcher;

    UCISearchReceiver search_receiver;
};

#endif