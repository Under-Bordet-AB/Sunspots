#pragma once

#include <vector>
#include <ctime>
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

    bool getResult();
    void displayLive();
    void displayGraph();

   private:
    HttpClient client;
    Result cachedResult;
    std::time_t data_start_time = 0;

    Result parseResponse(const std::string &buffer);
    int getCurrentQuarterIndex();
};