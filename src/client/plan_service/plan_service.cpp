#include <iostream>
#include <iomanip>
#include <string>
#include <cmath>

#include "plan_service.hpp"
#include "../utils/utils.hpp"
#include "../../libs/json/cJSON.h"

namespace
{
bool responseSucceeded(const HttpResponse& response, const char* endpoint)
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

bool PlanService::getResult()
{
    HttpResponse response = client.get(Config::PATH_SHOW_RESULT);
    if (responseSucceeded(response, Config::PATH_SHOW_RESULT))
        return false;

    cachedResult = parseResponse(response.body);
    return true;
}

void PlanService::displayLive()
{
    std::string currentTime = formatUnixTime(std::time (nullptr));

    int index = getCurrentQuarterIndex();
    if (index < 0 || index >= 96 || cachedResult.buy_electricity.size() < 96)
    {
        std::cerr << "Error: invalid data\n";
        return;
    }

    printOutline();

    printRow("LIVE STATUS", "");
    printRow("", "");
    printRow("Buy electricity: ",   formatDouble(cachedResult.buy_electricity[index], 2));
    printRow("Charge battery: ",    formatDouble(cachedResult.charge_battery[index], 2));
    printRow("Sell excess: ",       formatDouble(cachedResult.sell_excess[index], 2));
    printRow("Direct use: ",        formatDouble(cachedResult.direct_use[index], 2));
    printRow("", "");
    printRow("Data received: ",     cachedResult.timestamp);
    printRow("Current time: ",      currentTime);
    printRow("Quarter: ",           std::to_string(index));
    printRow("", "");
    printRow("Back < ", "");

    printOutline();

}

void PlanService::displayGraph()
{
    constexpr double y_axis = 21;
    constexpr double x_axis = 96;

    std::cout << " " << std::string(104, '_') << std::endl;
    for (int i = 0; i < y_axis; i++)     // Y-Axis
    {
        double resultValue = std::round((1.0 - (i * 0.05)) * 20.0) / 20.0;
        std::cout << "| " << std::fixed << std::setprecision(2) << resultValue << " - ";

        for (int j = 0; j < x_axis; j++) // X-Axis
        {
            // Determine if a dot should be drawn at this coordinate
            auto shouldDraw = [&](const std::vector<double>& data) -> bool 
            {
                double currentVal = roundToTenth(data[j]);
                
                if (resultValue == currentVal)
                    return true;
                
                // Draw connecting line
                if (j > 0)
                {
                    double prevVal = roundToTenth(data[j-1]);
                    if (prevVal != currentVal)
                    {
                        double minVal = std::min(prevVal, currentVal);
                        double maxVal = std::max(prevVal, currentVal);
                        
                        // Draw if we're between the two values
                        if (resultValue > minVal && resultValue < maxVal)
                            return true;
                    }
                }
                
                return false;
            };
            
            if (shouldDraw(cachedResult.buy_electricity))
            {
                std::cout << "\033[31m●\033[0m";
            }
            else if (shouldDraw(cachedResult.direct_use))
            {
                std::cout << "\033[32m●\033[0m";
            }
            else if (shouldDraw(cachedResult.charge_battery))
            {
                std::cout << "\033[33m●\033[0m";
            }
            else if (shouldDraw(cachedResult.sell_excess))
            {
                std::cout << "\033[34m●\033[0m";
            }
            else if (i == y_axis - 1)
            {
                std::cout << "-";
            }
            else
            {
                std::cout << "-";
            }
        }

        resultValue -= 0.05;
        std::cout << "|" << std::endl;
    }
    
    // Hour tick marks
    std::cout << "         ";
    for (int j = 0; j <= x_axis; j++)
    {
        if (j % 4 == 0)  // Every hour (4 quarters = 1 hour)
            std::cout << "|";
        else
            std::cout << " ";
    }
    std::cout << std::endl;
    
    // Time labels
    std::cout << "      ";
    for (int j = 0; j <= x_axis; j++)
    {
        if (j % 12 == 0 && j + 1 < x_axis)  // Every 3 hours
        {
            // Calculate actual time for this quarter
            std::time_t quarterTime = dataStartTime + (j * 15 * 60);
            std::tm* tm = std::localtime(&quarterTime);
            
            std::cout << " " << std::setw(2) << std::setfill('0') << tm->tm_hour << ":" 
                      << std::setw(2) << std::setfill('0') << tm->tm_min << std::setfill(' ');
            j += 5; // Skip positions
        }
        else if (j == x_axis)
        {
            std::time_t quarterTime = dataStartTime + (x_axis * 15 * 60);
            std::tm* tm = std::localtime(&quarterTime);
            
            std::cout << " " << std::setw(2) << std::setfill('0') << tm->tm_hour << ":" 
                      << std::setw(2) << std::setfill('0') << tm->tm_min << std::setfill(' ');
            break;
        }
        else
        {
            std::cout << " ";
        }
    }
    std::cout << std::endl;
    std::cout << "\n        \033[31m ● Buy Electricity\033[0m        \033[32m● Direct Use\033[0m        \033[33m● Charge Battery\033[0m        \033[34m● Sell Excess\033[0m" << std::endl;
    std::cout << "Back < " << std::endl;
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
        !cJSON_IsArray(sel))
    {
        cJSON_Delete(root);
        return Result{};
    }
    
    // Handle timestamp as either a number or an array
    std::time_t ts = 0;
    if (cJSON_IsNumber(tim))
    {
        ts = static_cast<std::time_t>(tim->valuedouble);
    }
    else if (cJSON_IsArray(tim))
    {
        cJSON *firstTimestamp = cJSON_GetArrayItem(tim, 0);
        if (firstTimestamp && cJSON_IsNumber(firstTimestamp))
        {
            ts = static_cast<std::time_t>(firstTimestamp->valuedouble);
        }
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
            else if (cJSON_IsNull(item))
            {
                vec.push_back(0.0);
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

    result.timestamp = formatUnixTime(ts);
    dataStartTime = ts;  // Store the data start time for quarter offset calculation
    
    cJSON_Delete(root);
    return result;
}

int PlanService::getCurrentQuarterIndex()
{
    std::time_t now = std::time(nullptr);
    std::tm* tm = std::localtime(&now);

    int currentHour = tm->tm_hour;
    int currentMinute = tm->tm_min;
    int currentQuarter = currentHour * 4 + (currentMinute / 15);

    // Parse the data start timestamp to get its quarter
    std::tm* data_tm = std::localtime(&dataStartTime);
    int dataHour = data_tm->tm_hour;
    int dataMinute = data_tm->tm_min;
    int dataQuarter = dataHour * 4 + (dataMinute / 15);

    // Return the offset from data start
    int offset = currentQuarter - dataQuarter;
    
    // Handle day wraparound
    if (offset < 0)
        offset += 96;
    
    return offset;
}