/**
 * @file plan_service.hpp
 * @brief Solar optimization data service
 * 
 * Fetches, parses, and displays solar panel optimization recommendations
 * including when to buy electricity, charge batteries, use directly, or sell excess.
 */

#pragma once

#include <vector>
#include <ctime>
#include "config.hpp"
#include "../http/http_client.hpp"

/**
 * @struct Result
 * @brief Solar optimization recommendations for 24 hours
 * 
 * Contains 96 quarter-hour values (0.0 to 1.0) for each recommendation type,
 * representing a 24-hour forecast period.
 */
struct Result
{
    std::vector<double> buy_electricity;  ///< Recommendation to buy from grid (0-1)
    std::vector<double> direct_use;       ///< Recommendation for direct consumption (0-1)
    std::vector<double> charge_battery;   ///< Recommendation to charge battery (0-1)
    std::vector<double> sell_excess;      ///< Recommendation to sell to grid (0-1)

    std::string timestamp;  ///< Human-readable timestamp of data start
};

/**
 * @class PlanService
 * @brief Service for managing solar optimization data
 * 
 * Handles fetching data from the server, parsing JSON responses, and
 * rendering live status displays and 24-hour forecast graphs.
 */
class PlanService
{
   public:
    /**
     * @brief Construct plan service
     * @param client HTTP client for server communication
     */
    PlanService(HttpClient &client);

    /**
     * @brief Fetch latest optimization results from server
     * @return true if data was successfully fetched and parsed
     */
    bool getResult();
    
    /**
     * @brief Display current quarter's recommendations
     * 
     * Shows real-time values for the current 15-minute period.
     */
    void displayLive();
    
    /**
     * @brief Display 24-hour forecast graph
     * 
     * Renders color-coded line graph of all four recommendation types
     * across 96 quarters (24 hours).
     */
    void displayGraph();

   private:
    HttpClient client;         ///< HTTP client for API requests
    Result cachedResult;       ///< Most recently fetched data
    std::time_t dataStartTime; ///< Unix timestamp of data start

    /**
     * @brief Parse JSON response into Result structure
     * @param buffer Raw JSON response body
     * @return Parsed Result object (empty on failure)
     */
    Result parseResponse(const std::string &buffer);
    
    /**
     * @brief Calculate current quarter index
     * @return Index (0-95) representing current 15-min period
     */
    int getCurrentQuarterIndex();
};