#pragma once

#include "input/input.hpp"
#include <vector>
#include <string>

class Menu
{
public:
    Menu();

    void mainMenu();

private:
    void clearScreen();
    int create(std::string title, std::vector<std::string> options);
};