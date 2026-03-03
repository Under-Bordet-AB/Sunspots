#include "http_request.hpp"

HttpRequest::HttpRequest(HttpMethod method, const std::string& host, const std::string& path)
    : method(method), host(host), path(path) {}

HttpRequest& HttpRequest::withBody(const std::string& body, const std::string& contentType)
{
    this->body = body;
    headers["Content-Type"] = contentType;
    headers["Content-Length"] = std::to_string(body.size());
    return *this;
}

HttpRequest& HttpRequest::withHeader(const std::string& key, const std::string& value)
{
    headers[key] = value;
    return *this;
}

std::string HttpRequest::build() const
{
    std::string request = methodToString() + " " + path + " HTTP/1.1\r\n";
    request += "Host: " + host + "\r\n";
    request += "User-Agent: HttpClient/1.0\r\n";
    request += "Accept: */*\r\n";
    request += "Connection: close\r\n";

    for (const auto& header : headers)
        request += header.first + ": " + header.second + "\r\n";

    request += "\r\n";

    if (!body.empty())
        request += body;

    return request;
}

std::string HttpRequest::methodToString() const
{
    switch (method)
    {
        case HttpMethod::GET:    return "GET";
        case HttpMethod::POST:   return "POST";
        case HttpMethod::PUT:    return "PUT";
        case HttpMethod::DELETE: return "DELETE";
        default:                 return "GET";
    }
}