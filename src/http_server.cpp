#include <iostream>
#include "http_server.h"

using namespace std;

HttpServer::HttpServer()
{
    cout << "Create HttpServer" << endl;
}

HttpServer::~HttpServer()
{}

HttpServer &HttpServer::GetInstance()
{
    static HttpServer httpServer;
    return httpServer;
}