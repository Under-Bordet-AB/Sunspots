#pragma once

#include <string>
#include <map>
#include "http_status.hpp"

class HttpResponse
{
   public:
    static HttpResponse parse(const std::string& raw);
    static HttpResponse error(const std::string& reason);

    bool isSuccess() const;
    bool hasError() const;

    StatusCode statusCode = StatusCode::OK;
    std::map<std::string, std::string> headers;
    std::string body;
    std::string errorMessage;

   private:
    bool parseError = false;
};