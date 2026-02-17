#include "menu.hpp"
#include <iostream>

Menu::Menu()
    : selected(0)
{}

void Menu::main()
{
    std::vector<std::string> options = { "Option 1", "Option 2", "Option 3", "Option 4", "Exit" };

    int selection = create("MAIN MENU", options);
    switch (selection)
    {
    case 0:
        /* code */
        break;
    case 1:
        /* code */
        break;
    case 2:
        /* code */
        break;
    case 3:
        /* code */
        break;
    case 4:
        /* code */
        break;
    }
}

// Creates an dynamic interactable list
// Returns the index in the vector that was selected
int Menu::create(std::string title, std::vector<std::string> options)
{
    size_t selected = 0;
    while (true)
    {
        clearScreen();
        
        std::cout << "* * * * * * *" << "\n";
        std::cout << "* " << title << std::endl;
        for (size_t i = 0; i < options.size(); i++)
        {
            if (i == selected)
                std::cout << "*  " << options[i] << " <\n"; // < indicator
            else
                std::cout << "* " << options[i] << "\n";
        }
        std::cout << "* * * * * * *" << std::endl;

        Input::Key keyPressed = Input::getArrowKey();

        if (keyPressed == Input::Key::UP && selected > 0)
            selected--;

        if (keyPressed == Input::Key::DOWN && selected < options.size() - 1)
            selected++;

        if (keyPressed == Input::Key::ENTER)
        {
            clearScreen();
            return static_cast<int>(selected);
        }
    }
}

void Menu::clearScreen()
{
    std::cout << "\033[2J\033[H";
}