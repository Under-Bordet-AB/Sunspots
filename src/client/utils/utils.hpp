/**
 * @file utils.hpp
 * @brief Utility functions for formatting and display
 * 
 * Provides helper functions for console output formatting, numeric conversion,
 * and time display.
 */

#pragma once

#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>

/**
 * @brief Print formatted two-column row
 * @param key Left column content
 * @param value Right column content
 */
void printRow(const std::string& key, const std::string& value);

/**
 * @brief Print horizontal border line
 */
void printOutline();

/**
 * @brief Format double to string with fixed precision
 * @param v Value to format
 * @param precision Number of decimal places
 * @return Formatted string
 */
std::string formatDouble(double v, int precision);

/**
 * @brief Convert Unix timestamp to readable date/time string
 * @param unixTime Unix timestamp (seconds since epoch)
 * @return Formatted string (YYYY-MM-DD HH:MM:SS)
 */
std::string formatUnixTime(std::time_t unixTime);

/**
 * @brief Round value to nearest 0.05
 * @param value Input value
 * @return Rounded value
 */
double roundToTenth(double value);