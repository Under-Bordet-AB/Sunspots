#include <iostream>
#include <iomanip>
#include <string>

#include "plan_service.hpp"
#include "../../libs/json/cJSON.h"

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
                  << static_cast<int>(response.statusCode) << "\n"
                  << "Press enter to return to menu\n";
        return true;
    }

    return false;
}
}

PlanService::PlanService(HttpClient &client) : client(client){}

void PlanService::showNow()
{
    HttpResponse response = client.get(Config::PATH_SHOW_NOW);
    if (reportIfFailed(response, Config::PATH_SHOW_NOW))
        return;

    // Parse response and put in Result struct
    Result result = parseResponse(response.body);

    // Display result
    std::cout << std::fixed << std::setprecision(2)
              << "Buy electricity: " << result.buy_electricity << "\n"
              << "Charge battery: " << result.charge_battery << "\n"
              << "Sell excess: " << result.sell_excess << "\n"
              << "Direct use: " << result.direct_use << "\n"
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

Result PlanService::parseResponse(const std::string &buffer)
{
    cJSON *root = cJSON_Parse(buffer.c_str());
    if (root == NULL)
    {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL)
        {
            std::cout << "Error: " << error_ptr << "\n";
        }
        cJSON_Delete(root);
        return Result{};
    }

    Result result;

    cJSON *resultObj = cJSON_GetObjectItemCaseSensitive(root, "result");
    if (!cJSON_IsObject(resultObj))
    {
        cJSON_Delete(root);
        return Result{};
    }

    cJSON *buy = cJSON_GetObjectItemCaseSensitive(resultObj, "buy_electricity");
    cJSON *dir = cJSON_GetObjectItemCaseSensitive(resultObj, "direct_use");
    cJSON *cha = cJSON_GetObjectItemCaseSensitive(resultObj, "charge_battery");
    cJSON *sel = cJSON_GetObjectItemCaseSensitive(resultObj, "sell_excess");
    cJSON *tim = cJSON_GetObjectItemCaseSensitive(resultObj, "timestamp");


    if (!cJSON_IsNumber(buy) ||
        !cJSON_IsNumber(dir) ||
        !cJSON_IsNumber(cha) ||
        !cJSON_IsNumber(sel) ||
        !cJSON_IsString(tim) ||
        tim->valuestring == nullptr)
    {
        cJSON_Delete(root);
        return Result{};
    }

    result.buy_electricity = buy->valuedouble;
    result.direct_use =      dir->valuedouble;
    result.charge_battery =  cha->valuedouble;
    result.sell_excess =     sel->valuedouble;
    std::string timestamp =  tim->valuestring;
    if (timestamp.size() >= 19 && timestamp[10] == 'T')
    {
        result.date = timestamp.substr(0, 10);
        result.time = timestamp.substr(11, 8);
    }
    
    cJSON_Delete(root);
    return result;
}