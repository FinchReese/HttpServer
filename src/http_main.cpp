#include <stdio.h>
#include "http_server.h"

int main()
{
    HttpServer &server = HttpServer::GetInstance();
    server.Run("127.0.0.1", 443, 5, 5);
    return 0;
}