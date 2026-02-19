#include <iostream>

#include "http/http_client.hpp"
#include "plan_service/plan_service.hpp"
#include "menu/menu.hpp"

int main()
{
    // ========== Connect to server ==========
    HttpClient client("localhost", "10480");
    if (!client.connect())
    {
        perror("Connect()");
        return 1;
    }

    // ======= Initialize plan service =======
    PlanService service(client);

    // ============= Start menu ==============
    Menu menu;
    menu.show(service);

    return 0;
}