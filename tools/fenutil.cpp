#include "fenutil.h"

#include <fstream>

#include "cinstance.h"

void load_fen_strings(std::vector<std::string>& strings, const std::string& path)
{
    std::ifstream file(path);
    std::string line;

    while (std::getline(file, line)) {
        if (line.empty())
            continue;
        strings.push_back(line);
    }
}