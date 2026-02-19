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

#include "config.hpp"

class HttpClient
{
   public:
    HttpClient(const std::string& host, const std::string& port);
    ~HttpClient();

    int connect();
    std::string get(const std::string& path);

   private:
    std::string host;
    std::string port;
    int fd;

    int write(const void* buffer, size_t length);
    int read(void* buffer, size_t length);
    void disconnect();

    std::string buildHttpRequest(const std::string& method, const std::string& path, const std::string& body = "");
};