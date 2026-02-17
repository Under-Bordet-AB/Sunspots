#include "menu.hpp"
#include <iostream>

#ifdef _WIN32
    #include <conio.h>
#else
    #include <termios.h>
    #include <unistd.h>
#endif

Key getArrowKey() 
{

    #ifdef _WIN32
        int ch = _getch();

        if (ch == 27)   return KEY_ESCAPE;
        if (ch == 13)   return KEY_ENTER;
        if (ch == 0 || ch == 224) 
        {
            ch = _getch();
            switch (ch) 
            {
                case 72: return KEY_UP;
                case 80: return KEY_DOWN;
                case 75: return KEY_LEFT;
                case 77: return KEY_RIGHT;
            }
        }

    #else
    termios oldt{}, newt{};
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;

    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    char c;
    read(STDIN_FILENO, &c, 1);

    // Escape
    if (c == 27) 
    {
        char next1, next2;
        if (read(STDIN_FILENO, &next1, 1) == 0) 
        {
            tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
            return KEY_ESCAPE;
        }

        if (next1 == '[' && read(STDIN_FILENO, &next2, 1)) 
        {
            tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
            switch (next2) {
                case 'A': return KEY_UP;
                case 'B': return KEY_DOWN;
                case 'C': return KEY_RIGHT;
                case 'D': return KEY_LEFT;
            }
        }

        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        return KEY_ESCAPE;
    }

    if (c == '\n') 
    {
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        return KEY_ENTER;
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
#endif

    return KEY_NONE;
}