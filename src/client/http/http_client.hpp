/**
 * @file http_client.hpp
 * @brief HTTP client implementation using POSIX sockets
 * 
 * Lightweight HTTP/1.1 client with support for GET and POST requests.
 * Manages TCP connections and handles request/response lifecycle.
 */

#pragma once

#include <cstring>
#include <iostream>
#include <string>

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "http_request.hpp"
#include "http_response.hpp"

/**
 * @class HttpClient
 * @brief TCP-based HTTP/1.1 client
 * 
 * Manages HTTP connections using POSIX sockets. Creates new connections
 * for each request (Connection: close). Suitable for local or low-frequency
 * requests.
 * 
 * @note Not thread-safe. Create separate instances for concurrent use.
 */
class HttpClient
{
   public:
    /**
     * @brief Construct HTTP client
     * @param host Server hostname or IP address
     * @param port Server port number (as string)
     */
    HttpClient(const std::string& host, const std::string& port);
    
    /**
     * @brief Destructor - closes any open connection
     */
    ~HttpClient();

    /**
     * @brief Establish TCP connection to server
     * @return true if connection successful or already connected
     */
    bool connect();
    
    /**
     * @brief Close current connection
     */
    void disconnect();

    /**
     * @brief Send HTTP GET request
     * @param path Request path (e.g., "/api/data")
     * @return Parsed HTTP response
     */
    HttpResponse get(const std::string& path);
    
    /**
     * @brief Send HTTP POST request
     * @param path Request path
     * @param body Request body content
     * @return Parsed HTTP response
     */
    HttpResponse post(const std::string& path, const std::string& body);

   private:
    std::string host;   ///< Server hostname
    std::string port;   ///< Server port
    int fd;             ///< Socket file descriptor (-1 if disconnected)

    /**
     * @brief Send request and receive response
     * @param request Constructed HTTP request
     * @return Parsed HTTP response
     */
    HttpResponse send(const HttpRequest& request);
    
    /**
     * @brief Receive complete HTTP response
     * @return Raw response string
     * @warning May block indefinitely if server doesn't close connection
     */
    std::string receive();
};