#pragma once

// C++ libraries
#include <cstring>
#include <iostream>
#include <string>

// C libraries
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "http_request.hpp"
#include "http_response.hpp"

class HttpClient
{
   public:
    HttpClient(const std::string& host, const std::string& port);
    ~HttpClient();

    bool connect();
    void disconnect();

    HttpResponse get(const std::string& path);
    HttpResponse post(const std::string& path, const std::string& body);

   private:
    std::string host;
    std::string port;
    int fd;

    HttpResponse send(const HttpRequest& request);
    std::string receive();
};