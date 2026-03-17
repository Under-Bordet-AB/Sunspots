/**
 * @file config.hpp
 * @brief Client configuration constants
 * 
 * Defines network connection parameters and API endpoints for the Sunspots client.
 */

#pragma once

/**
 * @namespace Config
 * @brief Client configuration namespace
 * 
 * Contains compile-time constants for server connection and API paths.
 */
namespace Config
{
    /** @brief Server hostname or IP address */
    constexpr const char *HOST = "localhost";
    
    /** @brief Server port number */
    constexpr const char *PORT = "10480";
    
    /** @brief API endpoint for fetching solar optimization results */
    constexpr const char *PATH_SHOW_RESULT = "/endpoints/result.json";
}