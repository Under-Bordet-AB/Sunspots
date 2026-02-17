#include "http/http_client.hpp"
#include "menu/menu.hpp"
#include <iostream>

int main()
{
    Menu menu;
    menu.main();

    HttpClient httpClient(HOST, PORT);
    std::string response = httpClient.get("/option1.txt");
    std::cout << response << "\n";
    
    return 0;
}