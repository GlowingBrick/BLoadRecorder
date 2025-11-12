#include <iostream>
#include <fstream>
#include <memory>
#include <vector>
#include <regex>
#include <vector>
#include <string>
#include <map>
#include <algorithm>
#include <set>
#include <sstream>
#include <fstream>
#include "lib/json/single_include/nlohmann/json.hpp"
#include "draw_svg.hpp"
#include <climits>

int main()
{
    std::ifstream file("monitor_test.json");

    nlohmann::json result;
    file >> result;
    file.close();

    draw_svg(result,"text");
}