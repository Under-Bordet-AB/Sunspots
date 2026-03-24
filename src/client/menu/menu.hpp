/**
 * @file menu.hpp
 * @brief Interactive terminal menu system
 * 
 * Provides a text-based user interface with arrow key navigation for
 * viewing solar optimization data.
 */

#pragma once

#include <string>
#include <vector>

#include "../utils/input.hpp"
#include "../plan_service/plan_service.hpp"

/**
 * @class Menu
 * @brief Interactive console menu
 * 
 * Displays navigable menu options and handles user input for viewing
 * live status and graphs of solar optimization recommendations.
 */
class Menu
{
   public:
    Menu();

    /**
     * @brief Display menu and handle user interactions
     * @param PlanService Service for fetching and displaying solar data
     */
    void show(PlanService &PlanService);

   private:
    /**
     * @brief Clear terminal screen
     */
    void clearScreen();
    
    /**
     * @brief Create and display selectable menu
     * @param options List of menu option labels
     * @return Index of selected option
     */
    int createAndSelect(std::vector<std::string> options);
};