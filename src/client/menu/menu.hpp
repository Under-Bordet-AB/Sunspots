#pragma once

#include <string>
#include <vector>

#include "../utils/input.hpp"
#include "../plan_service/plan_service.hpp"

class Menu
{
   public:
    Menu();

    void show(PlanService &PlanService);

   private:
    void clearScreen();
    int createAndSelect(std::vector<std::string> options);
};