#include "menu.hpp"
#include "../http/http_client.hpp"
#include <iostream>

Menu::Menu()
    : selected(0)
{
    main();
}

void Menu::main()
{
    std::vector<std::string> options = { "Check health", "Option 2", "Option 3", "Option 4", "Exit" };

    int selection = create("MAIN MENU", options);
    switch (selection)
    {
        case 0:
        {
            HttpClient httpClient(HOST, PORT);
            std::string response = httpClient.get("/health");
            std::cout << response << "\n";
            break;
        }
        case 1:
        {
            // Option 2
            break;
        }
        case 2:
        {
            // Option 3
            break;
        }
        case 3:
        {
            // Option 4
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

// Creates, displays and selects in a dynamic interactable list
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


        // Input handling
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