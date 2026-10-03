#ifndef TEST_H_INCLUDED
#define TEST_H_INCLUDED

#include <algorithm>
#include <vector>

#include "search.h"

class TestPosition
{
  public:
    TestPosition(const std::string& name,
                 const std::string& fen,
                 const std::vector<Move>& bms,
                 const std::vector<Move>& ams,
                 const std::string& comment)
        : test_name(name)
        , fen_str(fen)
        , c0(comment)
        , bests(bms)
        , avoids(ams)
    {
        assert(!name.empty());
        assert(!bests.empty() || !avoids.empty());
    }

    inline const std::string& name() const { return test_name; }
    inline const std::string& fen() const { return fen_str; }
    inline const std::string& comment() const { return c0; }
    inline const std::vector<Move>& best_moves() const { return bests; }
    inline const std::vector<Move>& avoid_moves() const { return avoids; }
    inline bool is_solved_by(Move move) const
    {
        return (bests.empty() || std::find(bests.begin(), bests.end(), move) != bests.end()) &&
               std::find(avoids.begin(), avoids.end(), move) == avoids.end();
    }

  private:
    std::string test_name, fen_str, c0;
    std::vector<Move> bests;
    std::vector<Move> avoids;
};

class TestSuite
{
  public:
    TestSuite(const std::string& name)
        : suite_name(name)
    {
        assert(!name.empty());
    }

    static TestSuite from_file(const std::string& path);

    inline void add(const TestPosition& p) { positions.push_back(p); }

    inline const TestPosition* begin() const { return &positions[0]; }
    inline const TestPosition* end() const { return &positions[positions.size()]; }

    inline size_t size() const { return positions.size(); }
    inline const std::string& name() const { return suite_name; }

  private:
    std::string suite_name;
    std::vector<TestPosition> positions;
};

class Tester
{
  public:
    Tester();

    // Returns the number of positions passed.
    size_t start_test(const TestSuite& suite, SearchMaster& searcher, const SearchConditions& conditions);

  private:
};

#endif