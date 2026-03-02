#include <iostream>
#include <iomanip>
#include <string>

#include "plan_service.hpp"
#include "../utils/utils.hpp"
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
    // HttpResponse response = client.get(Config::PATH_SHOW_NOW);
    // if (reportIfFailed(response, Config::PATH_SHOW_NOW))
    //     return;

    // // Parse response and put in Result struct
    // Result result = parseResponse(response.body);

    Result result = parseResponse("{\"result\":{\"buy_electricity\":[0.6,0.6,0.6,0.6,0.6,0.6,0.6,0.6,0.6,0.6,0.6,0.6,0.6,0.6,0.6,0.6,0.6,0.6,0.6,0.6,0.6,0.6,0.6,0.6,0.6,0.6,0.6,0.6,0.6,0.6,0.6,0.6,0.6,0.6,0.593475,0.58695,0.580425,0.5739,0.551625,0.52935,0.507075,0.4848,0.45825,0.4317,0.40515,0.3786,0.3552,0.3318,0.3084,0.285,0.28095,0.2769,0.27285,0.2688,0.282975,0.29715,0.311325,0.3255,0.32415,0.3228,0.32145,0.3201,0.323475,0.32685,0.330225,0.3336,0.363525,0.39345,0.423375,0.4533,0.475575,0.49785,0.520125,0.5424,0.555225,0.56805,0.580875,0.5937,0.595275,0.59685,0.598425,0.6,0.6,0.6,0.6,0.6,0.6,0.6,0.6,0.6,0.6,0.6,0.6,0.6,0.6,0.6],\"direct_use\":[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0.006525,0.01305,0.019575,0.0261,0.048375,0.07065,0.092925,0.1152,0.14175,0.1683,0.19485,0.2214,0.2448,0.2682,0.2916,0.315,0.31905,0.3231,0.32715,0.3312,0.317025,0.30285,0.288675,0.2745,0.27585,0.2772,0.27855,0.2799,0.276525,0.27315,0.269775,0.2664,0.236475,0.20655,0.176625,0.1467,0.124425,0.10215,0.079875,0.0576,0.044775,0.03195,0.019125,0.0063,0.004725,0.00315,0.001575,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],\"charge_battery\":[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],\"sell_excess\":[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],\"timestamp\":1772056619}}");

    std::string currentTime = formatUnixTime(std::time (nullptr));

    int index = getCurrentQuarterIndex();
    if (index <= 0 || index >= 96 || result.buy_electricity.size() < 96)
    {
        std::cerr << "Error: invalid data\n";
        return;
    }

    printOutline();

    printRow("LIVE STATUS", "");
    printRow("", "");
    printRow("Buy electricity: ",   formatDouble(result.buy_electricity[index], 2));
    printRow("Charge battery: ",    formatDouble(result.charge_battery[index], 2));
    printRow("Sell excess: ",       formatDouble(result.sell_excess[index], 2));
    printRow("Direct use: ",        formatDouble(result.direct_use[index], 2));
    printRow("", "");
    printRow("Data received: ",     result.timestamp);
    printRow("Current time: ",      currentTime);
    printRow("Quarter: ",           std::to_string(index));
    printRow("", "");
    printRow("Back < ", "");

    printOutline();

}

void PlanService::showDay()
{
    HttpResponse response = client.get(Config::PATH_SHOW_DAY);
    if (reportIfFailed(response, Config::PATH_SHOW_DAY))
        return;
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


    if (!cJSON_IsArray(buy) ||
        !cJSON_IsArray(dir) ||
        !cJSON_IsArray(cha) ||
        !cJSON_IsArray(sel) ||
        !cJSON_IsNumber(tim))
    {
        cJSON_Delete(root);
        return Result{};
    }

    auto fillVector = [](cJSON* array, std::vector<double> &vec)
    {
        cJSON *item = nullptr;
        cJSON_ArrayForEach(item, array)
        {
            if (cJSON_IsNumber(item))
            {
                vec.push_back(item->valuedouble);
            }
        }
    };

    result.buy_electricity.reserve(96);
    result.direct_use.reserve(96);
    result.charge_battery.reserve(96);
    result.sell_excess.reserve(96);

    fillVector(buy, result.buy_electricity);
    fillVector(dir, result.direct_use);
    fillVector(cha, result.charge_battery);
    fillVector(sel, result.sell_excess);

    std::time_t ts = static_cast<std::time_t>(tim->valuedouble);
    result.timestamp = formatUnixTime(ts);
    
    cJSON_Delete(root);
    return result;
}

int PlanService::getCurrentQuarterIndex()
{
    std::time_t now = std::time(nullptr);
    std::tm* tm = std::localtime(&now);

    int hour = tm->tm_hour;
    int minute = tm->tm_min;

    return hour * 4 + (minute / 15);
}