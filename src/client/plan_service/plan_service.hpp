#pragma once

#include <vector>
#include "config.hpp"
#include "../http/http_client.hpp"

struct Result
{
    std::vector<double> buy_electricity;
    std::vector<double> direct_use;
    std::vector<double> charge_battery;
    std::vector<double> sell_excess;

    std::string timestamp;
};

class PlanService
{
   public:
    PlanService(HttpClient &client);

    void getNow();
    void displayNow();
    void showDay();

   private:
    HttpClient client;
    Result cachedResult;

    Result parseResponse(const std::string &buffer);
    int getCurrentQuarterIndex();
};