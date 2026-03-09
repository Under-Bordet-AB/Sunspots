#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>

#include "menu.hpp"
#include "../plan_service/plan_service.hpp"
#include "../utils/utils.hpp"

Menu::Menu() {}

// Loads and shows menu with interactable options
void Menu::show(PlanService &service)
{
    std::vector<std::string> options = {"Live status", "Graph", "Exit"};

    while (true)
    {
        int selection = createAndSelect(options);

        switch (selection)
        {
            case 0:
            {
                // Option 1
                clearScreen();
                if (!service.getResult()) break;
                
                std::atomic<bool> running(true);
                std::thread updater([&]()
                {
                    while (running)
                    {
                        clearScreen();
                        service.displayLive();

                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                });

                while (Input::getArrowKey() != Input::Key::ENTER)
                {
                    // Don't do anything
                }

                running = false;
                updater.join(); 

                break;
            }
            case 1:
            {
                // Option 2
                clearScreen();
                if (!service.getResult()) break;
                service.displayGraph();

                while (Input::getArrowKey() != Input::Key::ENTER)
                {
                    // Don't do anything
                }

                break;
            }
            case 2:
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
int Menu::createAndSelect(std::vector<std::string> options)
{
    size_t selected = 0;

    while (true)
    {
        clearScreen();

        // ============= Printing =============
        printOutline();
        printRow("MAIN MENU", "");
        printRow("", "");
        for (size_t i = 0; i < options.size(); i++)
        {
            if (i == selected)
                printRow(options[i] + " <", "");  // < indicator
            else
                printRow(options[i], "");
        }
        printOutline();

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