/**
 * @file plan_service.cpp
 * @brief Solar optimization service implementation
 */

#include <iostream>
#include <iomanip>
#include <string>
#include <cmath>

#include "plan_service.hpp"
#include "../utils/utils.hpp"
#include "../../libs/json/cJSON.h"

namespace
{
/**
 * @brief Check if HTTP response indicates an error
 * @param response HTTP response object
 * @param endpoint Endpoint name for error messages
 * @return true if response has error (and message was printed)
 */
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
    constexpr double y_axis = 21; // From 0.0 to 1.0, +0.05 per line
    constexpr double x_axis = 96; // 24 hours * 4 quarters/hour

    std::cout << " " << std::string(104, '_') << std::endl;
    for (int i = 0; i < y_axis; i++)     // Y-Axis
    {
        // Calculate value for this row (1.00 at top, 0.00 at bottom)
        double resultValue = std::round((1.0 - (i * 0.05)) * 20.0) / 20.0;
        std::cout << "| " << std::fixed << std::setprecision(2) << resultValue << " - ";

        for (int j = 0; j < x_axis; j++) // X-Axis
        {
            // Lambda to check if we should draw at this coordinate
            auto shouldDraw = [&](const std::vector<double>& data) -> bool 
            {
                double currentVal = roundToTenth(data[j]);
                
                if (resultValue == currentVal)
                    return true;
                
                // Draw vertical connecting line between data points
                if (j > 0)
                {
                    double prevVal = roundToTenth(data[j-1]);
                    if (prevVal != currentVal)
                    {
                        double minVal = std::min(prevVal, currentVal);
                        double maxVal = std::max(prevVal, currentVal);
                        
                        // Draw if current row is between previous and current value
                        if (resultValue > minVal && resultValue < maxVal)
                            return true;
                    }
                }
                
                return false;
            };
            
            // Draw colored dots based on data type (ANSI color codes)
            if (shouldDraw(cachedResult.buy_electricity))
            {
                std::cout << "\033[31m●\033[0m";  // Red
            }
            else if (shouldDraw(cachedResult.direct_use))
            {
                std::cout << "\033[32m●\033[0m";  // Green
            }
            else if (shouldDraw(cachedResult.charge_battery))
            {
                std::cout << "\033[33m●\033[0m";  // Yellow
            }
            else if (shouldDraw(cachedResult.sell_excess))
            {
                std::cout << "\033[34m●\033[0m";  // Blue
            }
            else
            {
                std::cout << "-";  // Empty space
            }
        }

        std::cout << "|" << std::endl;
    }
    
    // Draw hour tick marks on X-axis
    std::cout << "         ";
    for (int j = 0; j <= x_axis; j++)
    {
        if (j % 4 == 0)  // Every 4 quarters = 1 hour
            std::cout << "|";
        else
            std::cout << " ";
    }
    std::cout << std::endl;
    
    // Draw time labels every 3 hours
    std::cout << "      ";
    for (int j = 0; j <= x_axis; j++)
    {
        if (j % 12 == 0 && j + 1 < x_axis)  // Every 3 hours
        {
            std::time_t quarterTime = dataStartTime + (j * 15 * 60);
            std::tm* tm = std::localtime(&quarterTime);
            
            std::cout << " " << std::setw(2) << std::setfill('0') << tm->tm_hour << ":" 
                      << std::setw(2) << std::setfill('0') << tm->tm_min << std::setfill(' ');
            j += 5; // Skip next 5 positions to avoid label overlap
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
    
    // Draw color legend
    std::cout << "\n        \033[31m ● Buy Electricity\033[0m        "
              << "\033[32m● Direct Use\033[0m        "
              << "\033[33m● Charge Battery\033[0m        "
              << "\033[34m● Sell Excess\033[0m" << std::endl;
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

    // Validate all arrays exist
    if (!cJSON_IsArray(buy) ||
        !cJSON_IsArray(dir) ||
        !cJSON_IsArray(cha) ||
        !cJSON_IsArray(sel))
    {
        cJSON_Delete(root);
        return Result{};
    }
    
    // Parse timestamp (can be either number or array)
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

    // Lambda to fill vector from cJSON array
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

    // Pre-allocate for 96 quarters (24 hours)
    result.buy_electricity.reserve(96);
    result.direct_use.reserve(96);
    result.charge_battery.reserve(96);
    result.sell_excess.reserve(96);

    fillVector(buy, result.buy_electricity);
    fillVector(dir, result.direct_use);
    fillVector(cha, result.charge_battery);
    fillVector(sel, result.sell_excess);

    result.timestamp = formatUnixTime(ts);
    dataStartTime = ts;  // Cache for quarter offset calculations
    
    cJSON_Delete(root);
    return result;
}

int PlanService::getCurrentQuarterIndex()
{
    std::time_t now = std::time(nullptr);
    
    // Calculate seconds elapsed since data start
    std::time_t elapsed = now - dataStartTime;
    
    // Convert to quarters (15 minutes = 900 seconds)
    int offset = static_cast<int>(elapsed / 900);
    
    // Clamp to valid range
    if (offset < 0) offset = 0;
    if (offset >= 96) offset = 95;
    
    return offset;
}