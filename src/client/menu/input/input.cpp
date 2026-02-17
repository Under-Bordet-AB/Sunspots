#include "input.hpp"

Input::Key Input::getArrowKey() 
{

    #ifdef _WIN32
        int ch = _getch();

        if (ch == 27)   return Input::Key::ESCAPE;
        if (ch == 13)   return Input::Key::ENTER;
        if (ch == 0 || ch == 224) 
        {
            ch = _getch();
            switch (ch) 
            {
                case 72: return Input::Key::UP;
                case 80: return Input::Key::DOWN;
                case 75: return Input::Key::LEFT;
                case 77: return Input::Key::RIGHT;
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
            return Input::Key::ESCAPE;
        }

        if (next1 == '[' && read(STDIN_FILENO, &next2, 1)) 
        {
            tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
            switch (next2) {
                case 'A': return Input::Key::UP;
                case 'B': return Input::Key::DOWN;
                case 'C': return Input::Key::RIGHT;
                case 'D': return Input::Key::LEFT;
            }
        }

        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        return Input::Key::ESCAPE;
    }

    if (c == '\n') 
    {
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        return Input::Key::ENTER;
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
#endif

    return Input::Key::NONE;
}