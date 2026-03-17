/**
 * @file input.hpp
 * @brief Cross-platform keyboard input handling
 * 
 * Provides platform-independent interface for reading arrow keys and special
 * keys from the terminal. Supports both Windows (using conio.h) and Unix-like
 * systems (using termios).
 */

#pragma once

#ifdef _WIN32
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

/**
 * @namespace Input
 * @brief Terminal input utilities
 */
namespace Input
{
/**
 * @enum Key
 * @brief Special keyboard keys
 */
enum class Key
{
    NONE,    ///< No special key pressed
    UP,      ///< Up arrow key
    DOWN,    ///< Down arrow key
    LEFT,    ///< Left arrow key
    RIGHT,   ///< Right arrow key
    ESCAPE,  ///< Escape key
    ENTER    ///< Enter/Return key
};

/**
 * @brief Read next key press
 * 
 * Blocks until a key is pressed. Recognizes arrow keys, Enter, and Escape.
 * On Unix systems, temporarily switches terminal to raw mode.
 * 
 * @return Key code of pressed key
 */
Key getArrowKey();
}  // namespace Input