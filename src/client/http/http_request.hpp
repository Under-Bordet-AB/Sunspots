/**
 * @file http_request.hpp
 * @brief HTTP request builder
 * 
 * Provides a fluent interface for constructing HTTP requests with custom
 * headers and body content.
 */

#pragma once

#include <string>
#include <map>

/**
 * @enum HttpMethod
 * @brief Supported HTTP methods
 */
enum class HttpMethod
{
    GET,     ///< HTTP GET method
    POST,    ///< HTTP POST method
    PUT,     ///< HTTP PUT method
    DELETE   ///< HTTP DELETE method
};

/**
 * @class HttpRequest
 * @brief Builder for HTTP/1.1 requests
 * 
 * Constructs properly formatted HTTP requests with support for custom headers
 * and request bodies. Uses fluent interface for method chaining.
 */
class HttpRequest
{
   public:
    /**
     * @brief Construct a new HTTP request
     * @param method HTTP method (GET, POST, etc.)
     * @param host Target hostname
     * @param path Request path (e.g., "/api/data")
     */
    HttpRequest(HttpMethod method, const std::string& host, const std::string& path);

    /**
     * @brief Add request body with content type
     * @param body Request body content
     * @param contentType MIME type (default: application/json)
     * @return Reference to this request for chaining
     */
    HttpRequest& withBody(const std::string& body, const std::string& contentType = "application/json");
    
    /**
     * @brief Add custom header
     * @param key Header name
     * @param value Header value
     * @return Reference to this request for chaining
     */
    HttpRequest& withHeader(const std::string& key, const std::string& value);

    /**
     * @brief Build the raw HTTP request string
     * @return Formatted HTTP/1.1 request ready to send
     */
    std::string build() const;

   private:
    HttpMethod method;                            ///< HTTP method
    std::string host;                             ///< Target hostname
    std::string path;                             ///< Request path
    std::string body;                             ///< Request body (optional)
    std::map<std::string, std::string> headers;   ///< Custom headers

    /**
     * @brief Convert HttpMethod enum to string
     * @return HTTP method as string (e.g., "GET")
     */
    std::string methodToString() const;
};