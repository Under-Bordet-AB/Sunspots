#pragma once

#ifdef _WIN32
    #include <conio.h>
#else
    #include <termios.h>
    #include <unistd.h>
#endif

namespace Input
{
    enum class Key
    {
        NONE,
        UP,
        DOWN,
        LEFT,
        RIGHT,
        ESCAPE,
        ENTER
    };

    Key getArrowKey();
}