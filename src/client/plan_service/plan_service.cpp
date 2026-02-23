#include <iostream>
#include <iomanip>
#include <string>

#include "plan_service.hpp"

namespace
{
bool reportIfFailed(const HttpResponse& response, const char* endpoint)
{
    if (response.hasError())
    {
        std::cerr << "Request to " << endpoint << " failed: " << response.errorMessage << "\n";
        return true;
    }

    if (!response.isSuccess())
    {
        std::cerr << "Request to " << endpoint << " failed with status "
                  << static_cast<int>(response.statusCode) << "\n";
        return true;
    }

    return false;
}
}

PlanService::PlanService(HttpClient &client) : client(client){}

void PlanService::showNow()
{
    Result result;
    HttpResponse response = client.get(Config::PATH_SHOW_NOW);
    if (reportIfFailed(response, Config::PATH_SHOW_NOW))
        return;

    // Parse response and put in Result struct

    // Display result
    std::cout << std::fixed << std::setprecision(2)
              << "Buy electricity: " << result.buy_electricity << "\n"
              << "Charge battery: " << result.charge_battery << "\n"
              << "Sell excess: " << result.sell_excess << "\n"
              << "Use solar: " << result.use_solar << "\n"
              << "Date: " << result.date << "\n"
              << "Time: " << result.time << "\n"
              << "Back < " << std::endl;
}

void PlanService::showDay()
{
    HttpResponse response = client.get(Config::PATH_SHOW_DAY);
    reportIfFailed(response, Config::PATH_SHOW_DAY);
}

void PlanService::showWeek()
{
    HttpResponse response = client.get(Config::PATH_SHOW_WEEK);
    reportIfFailed(response, Config::PATH_SHOW_WEEK);
}

void PlanService::showHistory()
{
    HttpResponse response = client.get(Config::PATH_SHOW_HISTORY);
    reportIfFailed(response, Config::PATH_SHOW_HISTORY);
}
