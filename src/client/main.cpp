/**
 * @file main.cpp
 * @brief Entry point for Sunspots terminal client
 * 
 * Initializes the HTTP client, plan service, and interactive menu system
 * for viewing solar optimization recommendations.
 */

#include <iostream>

#include "http/http_client.hpp"
#include "plan_service/plan_service.hpp"
#include "menu/menu.hpp"
#include "config.hpp"

int main()
{
    HttpClient client(Config::HOST, Config::PORT);
    PlanService service(client);
    Menu menu;
    menu.show(service);

    return 0;
}