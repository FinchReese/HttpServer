#ifndef HTTP_SERVER_H
#define HTTP_SERVER

class HttpServer {
public:
    static HttpServer &GetInstance();
    void Run(const char *ipAddr, const unsigned short int portId,  const unsigned int backlog, const int epollSize);
private:
    HttpServer();
    ~HttpServer();
    bool InitServer(const char *ipAddr, const unsigned short int portId,  const unsigned int backlog);
    bool InitEpollFd(const int epollSize);
    bool RegisterServerReadEvent();
    void EventLoop(const int epollSize);
    void HandleServerReadEvent();
    void HandleClientReadEvent(const int client);
    void clear();
private:
    int m_server { -1 }; // 记录socket服务器套接字，初始化为-1是无效值
    int m_efd { -1 };
};

#endif