#pragma once

#include <string>
#include <vector>

#include "input/input.hpp"
#include "../plan_service/plan_service.hpp"

class Menu
{
   public:
    Menu();

    void show(PlanService &PlanService);

   private:
    void clearScreen();
    int createAndSelect(std::string title, std::vector<std::string> options);
};