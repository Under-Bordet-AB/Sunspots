/**
 * @file http_response.hpp
 * @brief HTTP response parser and container
 * 
 * Parses and stores HTTP response data including status code, headers, and body.
 */

#pragma once

#include <string>
#include <map>
#include "http_status.hpp"

/**
 * @class HttpResponse
 * @brief HTTP response data container
 * 
 * Stores parsed HTTP response data and provides methods for validation
 * and error checking.
 */
class HttpResponse
{
   public:
    /**
     * @brief Parse raw HTTP response string
     * @param raw Raw HTTP response text
     * @return Parsed HttpResponse object
     */
    static HttpResponse parse(const std::string& raw);
    
    /**
     * @brief Create error response
     * @param reason Error description
     * @return HttpResponse with error flag set
     */
    static HttpResponse error(const std::string& reason);

    /**
     * @brief Check if response indicates success (2xx status)
     * @return true if status code is 200-299
     */
    bool isSuccess() const;
    
    /**
     * @brief Check if response has parse or connection error
     * @return true if error occurred before/during parsing
     */
    bool hasError() const;

    StatusCode statusCode = StatusCode::OK;           ///< HTTP status code
    std::map<std::string, std::string> headers;       ///< Response headers
    std::string body;                                 ///< Response body
    std::string errorMessage;                         ///< Error description (if any)

   private:
    bool parseError = false;                          ///< Parse error flag
};