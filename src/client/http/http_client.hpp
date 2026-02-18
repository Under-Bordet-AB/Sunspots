
// C++ libraries
#include <iostream>
#include <string>
#include <cstring>

// C libraries
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <fcntl.h>

#define HOST "localhost"
#define PORT "10480"
#define MAX_TIMEOUT_ATTEMPTS 100

class HttpClient
{
public:
    HttpClient(const std::string &host, const std::string &port);
    ~HttpClient();

    std::string get(const std::string &path);
    

private:
    int fd;
    std::string host;
    std::string port;

    int connect(const std::string &host, const std::string &port);
    int write(const void *buffer, size_t length);
    int read(void *buffer, size_t length);
    void disconnect();

    std::string buildHttpRequest(const std::string &method, 
                                 const std::string &host, 
                                 const std::string &path, 
                                 const std::string &body = "");

    
    
};