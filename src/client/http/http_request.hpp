#pragma once

#include <string>
#include <map>

enum class HttpMethod
{
    GET,
    POST,
    PUT,
    DELETE
};

class HttpRequest
{
   public:
    HttpRequest(HttpMethod method, const std::string& host, const std::string& path);

    HttpRequest& withBody(const std::string& body, const std::string& contentType = "application/json");
    HttpRequest& withHeader(const std::string& key, const std::string& value);

    std::string build() const;

   private:
    HttpMethod method;
    std::string host;
    std::string path;
    std::string body;
    std::map<std::string, std::string> headers;

    std::string methodToString() const;
};