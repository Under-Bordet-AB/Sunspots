#include "http_client.hpp"

HttpClient::HttpClient(const std::string& host, const std::string& port) 
    : fd(-1) 
{
    if (connect(host, port) < 0)
        perror("connect");
}

HttpClient::~HttpClient() 
{
    disconnect();
}

int HttpClient::connect(const std::string& host, const std::string& port) {
    if (this->fd >= 0) {
        perror("");
        return -1;
    }

    struct addrinfo hints = {};
    struct addrinfo* res = NULL;

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0)
        return -1;

    int fd = -1;
    for (addrinfo* rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

        if (fd < 0)
            continue;

        if (::connect(fd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;

        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);
    if (fd < 0) {
        perror("");
        return -1;
    }

    this->fd = fd;

    std::cout << "Client FD " << fd << " connected\n";
    return 0;
}

int HttpClient::write(const void* buffer, size_t length) {
    return send(fd, buffer, length, MSG_NOSIGNAL);  // Non-blocking
}

int HttpClient::read(void* buffer, size_t length) {
    return recv(fd, buffer, length, MSG_DONTWAIT);  // Non-blocking
}

void HttpClient::disconnect() {
    if (fd >= 0) {
        std::cout << "Client FD " << fd << " disconnected\n";
        close(fd);
        fd = -1;
    }
}

std::string HttpClient::get(const std::string &path)
{
    std::string request = buildHttpRequest("GET", HOST, path, "");
    write(request.c_str(), request.length());
    
    char buffer[1024];
    std::string response;

    ssize_t bytesReceived = 0;
    while(true)
    {
        bytesReceived = read(buffer, sizeof(buffer) - 1);
        
        if(bytesReceived > 0) {
            buffer[bytesReceived] = '\0';
            response += buffer;
        }
        else if(bytesReceived == 0) {
            break; // Connection closed
        }
        else if(errno == EAGAIN || errno == EWOULDBLOCK) {
            // No data, wait and try again
            usleep(1000); // 1 ms
            continue;
        }
        else {
            perror("read error");
            break;
        }
    }
    
    return response;
}

std::string HttpClient::buildHttpRequest(const std::string &method, const std::string &host, const std::string &path, const std::string &body) 
{
    std::string request = method + " " + path + " HTTP/1.1\r\n";
    request += "Host: " + host + "\r\n";
    request += "User-Agent: HttpClient/1.0\r\n";
    request += "Accept: */*\r\n";
    
    if (!body.empty()) {
        request += "Content-Length: " + std::to_string(body.length()) + "\r\n";
        request += "Content-Type: application/json\r\n";
    }
    
    request += "Connection: close\r\n";
    request += "\r\n";
    
    if (!body.empty()) {
        request += body;
    }
    
    return request;
}

