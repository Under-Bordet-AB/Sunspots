/**
 * @file menu.cpp
 * @brief Interactive menu implementation
 */

#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>

#include "menu.hpp"
#include "../plan_service/plan_service.hpp"
#include "../utils/utils.hpp"

Menu::Menu() {}

void Menu::show(PlanService &service)
{
    std::vector<std::string> options = {"Live status", "Graph", "Exit"};

    while (true)
    {
        int selection = createAndSelect(options);

        switch (selection)
        {
            case 0:  // Live status option
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

                // Wait for user to press Enter to stop
                while (Input::getArrowKey() != Input::Key::ENTER)
                {
                    // User can press any key, but only Enter exits
                }

                running = false;
                updater.join(); 

                break;
            }
            case 1:  // Graph option
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
            case 2:  // Exit option
            {
                std::cout << "Exiting program...\n";
                exit(EXIT_SUCCESS);
                break;
            }
        }
    }
}

int Menu::createAndSelect(std::vector<std::string> options)
{
    size_t selected = 0;

    while (true)
    {
        clearScreen();

        printOutline();
        printRow("MAIN MENU", "");
        printRow("", "");
        for (size_t i = 0; i < options.size(); i++)
        {
            if (i == selected)
                printRow(options[i] + " <", "");  // Visual selection indicator
            else
                printRow(options[i], "");
        }
        printOutline();

        // Handle navigation input
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
    std::cout << "\033[2J\033[H";  // ANSI escape codes: clear screen and move cursor to home
}