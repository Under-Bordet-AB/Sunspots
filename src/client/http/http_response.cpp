#include "http_response.hpp"

HttpResponse HttpResponse::parse(const std::string& raw)
{
    HttpResponse response;

    size_t statusEnd = raw.find("\r\n");
    if (statusEnd == std::string::npos)
        return HttpResponse::error("Bad response: no status line");

    // Parse status code
    std::string statusLine = raw.substr(0, statusEnd);
    size_t codeStart = statusLine.find(' ');
    if (codeStart == std::string::npos)
        return HttpResponse::error("Bad status line");

    response.statusCode = static_cast<StatusCode>(
        std::stoi(statusLine.substr(codeStart + 1, 3))
    );

    // Parse headers
    size_t pos = statusEnd + 2;
    while (pos < raw.size())
    {
        size_t lineEnd = raw.find("\r\n", pos);
        if (lineEnd == std::string::npos) break;

        std::string line = raw.substr(pos, lineEnd - pos);
        if (line.empty()) 
        { 
            pos = lineEnd + 2; 
            break; 
        }

        size_t sep = line.find(": ");
        if (sep != std::string::npos)
            response.headers[line.substr(0, sep)] = line.substr(sep + 2);

        pos = lineEnd + 2;
    }

    // Everything after blank line is body
    response.body = raw.substr(pos);

    return response;
}

HttpResponse HttpResponse::error(const std::string& reason)
{
    HttpResponse response;
    response.parseError = true;
    response.errorMessage = reason;
    response.statusCode = StatusCode::InternalServerError;
    return response;
}

bool HttpResponse::isSuccess() const
{
    int code = static_cast<int>(statusCode);
    return !parseError && code >= 200 && code < 300;
}

bool HttpResponse::hasError() const
{
    return parseError;
}