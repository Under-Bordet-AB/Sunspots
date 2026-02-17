#pragma once

#include "input/input.hpp"
#include <vector>
#include <string>

class Menu
{
public:
    Menu();

    void main();

private:
    size_t selected;
    void clearScreen();
    int create(std::string title, std::vector<std::string> options);

};