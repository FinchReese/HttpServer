#include <stdio.h>
#include "http_server.h"

const char *SOURCE_DIR = "/home/enspire/code/HttpServer/webpages";

int main()
{
    HttpServer &server = HttpServer::GetInstance();
    server.Run("127.0.0.1", 443, 5, 5, SOURCE_DIR);
    return 0;
}