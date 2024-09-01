#ifndef HTTP_SERVER_H
#define HTTP_SERVER

class HttpServer {
public:
    static HttpServer &GetInstance();
private:
    HttpServer();
    ~HttpServer();
};

#endif