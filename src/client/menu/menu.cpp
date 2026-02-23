#include "menu.hpp"
#include "../plan_service/plan_service.hpp"

#include <iostream>

Menu::Menu() {}

// Loads and shows menu with interactable options
void Menu::show(PlanService &service)
{
    std::vector<std::string> options = {"Now", "Day", "Week", "History", "Exit"};

    while (true)
    {
        int selection = createAndSelect("MAIN MENU", options);

        switch (selection)
        {
            case 0:
            {
                // Option 1
                // Show what to do right now
                service.showNow();
        
                while (Input::getArrowKey() != Input::Key::ENTER)
                {
                    clearScreen();
                }
                break;
            }
            case 1:
            {
                // Option 2
                // Show next 24 hours
                clearScreen();
                service.showDay();
                break;
            }
            case 2:
            {
                // Option 3
                // Show next 7 days
                service.showWeek();
                break;
            }
            case 3:
            {
                // Option 4
                // See history log
                service.showHistory();
                break;
            }
            case 4:
            {
                std::cout << "Exiting program...\n";
                exit(EXIT_SUCCESS);
                break;
            }
        }
    }
}

// Creates, displays and selects in a dynamic interactable list
// Returns the index in the vector that was selected
int Menu::createAndSelect(std::string title, std::vector<std::string> options)
{
    size_t selected = 0;
    std::string outline = "+----------------------+";

    while (true)
    {
        clearScreen();

        // ============= Printing =============
        std::cout << outline << "\n";
        std::cout << "|\t" << title << std::endl;
        for (size_t i = 0; i < options.size(); i++)
        {
            if (i == selected)
                std::cout << "|  " << options[i] << " <\n";  // < indicator
            else
                std::cout << "| " << options[i] << "\n";
        }
        std::cout << outline << std::endl;

        // ========== Input handling ==========
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