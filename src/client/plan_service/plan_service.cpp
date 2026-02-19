#include "plan_service.hpp"

PlanService::PlanService(HttpClient &client) : client(client){}

void PlanService::showNow()
{
    client.get(Config::PATH_SHOW_NOW);
}

void PlanService::showDay()
{
    client.get(Config::PATH_SHOW_DAY);
}

void PlanService::showWeek()
{
    client.get(Config::PATH_SHOW_WEEK);
}

void PlanService::showHistory()
{
    client.get(Config::PATH_SHOW_HISTORY);
}
