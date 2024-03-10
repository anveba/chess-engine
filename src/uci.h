#ifndef UCI_H_INCLUDED
#define UCI_H_INCLUDED

#include <sstream>
#include <string>

#include "board.h"
#include "search.h"
#include "timestrat.h"

class UCISearchReceiver : public ISearchReceiver
{
  public:
    void receive_search_result(const SearchResult& result) override;
};

class UCI
{
  public:
    UCI();
    void start();

  private:
    void position(std::istringstream& in);
    void go(std::istringstream& in);
    void start_perft(std::istringstream& in);
    void start_test(std::istringstream& in);

  private:
    Board board;

    SearchMaster searcher;

    UCISearchReceiver search_receiver;
};

#endif