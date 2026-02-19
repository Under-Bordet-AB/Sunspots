#pragma once

#include "config.hpp"
#include "../http/http_client.hpp"

struct LiveStatus
{
    double buy_electricity = false;
    double use_solar = false;
    double charge_battery = false;
    double sell_excess = false;
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