#include "http/http_client.hpp"
#include "menu/menu.hpp"
#include <iostream>

int main()
{
    HttpClient httpClient(HOST, PORT);
    
    std::string response = httpClient.get("/test.txt");
    std::cout << response << "\n";
    
    return 0;
}