#include <iostream>

#include "http/http_client.hpp"
#include "plan_service/plan_service.hpp"
#include "menu/menu.hpp"
#include "config.hpp"

int main()
{
    // ========== Initialize server ========== //
    HttpClient client(Config::HOST, Config::PORT);

    // ======= Initialize plan service ======= //
    PlanService service(client);

    // ============= Start menu ============== //
    Menu menu;
    menu.show(service);

    return 0;
}