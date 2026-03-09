#include <ctime>
#include <cmath>
#include <sstream>
#include <iomanip>

#include "utils.hpp"

const int WIDTH = 46;

void printRow(const std::string& key, const std::string& value)
{
    std::cout << "| "
              << std::left << std::setw(20) << key
              << std::right << std::setw(22) << value
              << " |\n";
}

void printOutline()
{
    std::cout << "+" << std::string(WIDTH - 2, '-') << "+\n";
}

std::string formatDouble(double v, int precision)
{
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(precision) << v;
    return ss.str();
}

std::string formatUnixTime(std::time_t unixTime)
{
    std::tm* tm = std::localtime(&unixTime);

    std::ostringstream oss;
    oss << std::put_time(tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

double roundToTenth(double value) 
{
    return std::round(value * 20.0) / 20.0;
}