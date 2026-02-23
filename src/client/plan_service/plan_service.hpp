#pragma once

#include "config.hpp"
#include "../http/http_client.hpp"

struct Result 
{
    double buy_electricity = 0.0;
    double use_solar = 0.0;
    double charge_battery = 0.0;
    double sell_excess = 0.0;

    std::string date;
    std::string time;
};

class PlanService
{
   public:
    PlanService(HttpClient &client);

    void showNow();
    void showDay();
    void showWeek();
    void showHistory();

   private:
    HttpClient client;
};