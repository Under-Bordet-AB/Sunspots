#pragma once

#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>

void printRow(const std::string& key, const std::string& value);
void printOutline();
std::string formatDouble(double v, int precision);
std::string formatUnixTime(std::time_t unixTime);