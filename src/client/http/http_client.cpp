#include "http_client.hpp"

HttpClient::HttpClient(const std::string& host, const std::string& port) : host(host), port(port), fd(-1) {}

HttpClient::~HttpClient()
{
    disconnect();
}

bool HttpClient::connect()
{
    if (this->fd >= 0)
    {
        return true;
    }

    struct addrinfo hints = {};
    struct addrinfo* res = NULL;

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0)
        return false;

    int fd = -1;
    for (addrinfo* rp = res; rp; rp = rp->ai_next)
    {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

        if (fd < 0)
            continue;

        if (::connect(fd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;

        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);
    if (fd < 0)
    {
        return false;
    }

    this->fd = fd;

    std::cout << "Client FD " << fd << " connected\n";
    return true;
}

void HttpClient::disconnect()
{
    if (fd >= 0)
    {
        std::cout << "Client FD " << fd << " disconnected\n";
        close(fd);
        fd = -1;
    }
}

HttpResponse HttpClient::get(const std::string& path)
{
    if (!this->connect())
    {
        return HttpResponse::error("Connection failed to " + host + ":" + port);
    }
    else
    {
        HttpRequest request(HttpMethod::GET, host, path);
        return send(request);
    }
}

HttpResponse HttpClient::post(const std::string& path, const std::string& body)
{
    HttpRequest request(HttpMethod::POST, host, path);
    request.withBody(body);
    return send(request);
}

HttpResponse HttpClient::send(const HttpRequest& request)
{
    if (fd < 0 && !connect())
        return HttpResponse::error("Not connected");

    std::string raw = request.build();
    if (::send(fd, raw.c_str(), raw.size(), 0) < 0)
        return HttpResponse::error("Send failed");

    std::string response = receive();
    disconnect();

    return HttpResponse::parse(response);
}

std::string HttpClient::receive()
{
    std::string response;
    char buffer[4096];
    ssize_t bytes;

    while ((bytes = recv(fd, buffer, sizeof(buffer) - 1, 0)) > 0)
    {
        buffer[bytes] = '\0';
        response += buffer;
    }

    return response;
}
