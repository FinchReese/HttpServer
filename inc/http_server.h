#ifndef HTTP_SERVER_H
#define HTTP_SERVER
#include <string>
#include <map>
#include "http_processor.h"
#include "client_expire_min_heap.h"
#include "thread_pool.h"

class HttpServer;

struct HttpReqProcessArg {
    HttpServer *httpServer;
    HttpProcessor *httpProcessor;
    int client;
};

const unsigned int PIPE_FD_NUM = 2; // 一对能互相通信的scoket，数量为2

class HttpServer {
public:
    static HttpServer &GetInstance();
    void Run(const char *ipAddr, const unsigned short int portId,  const unsigned int backlog, const int epollSize,
        const char *sourceDir);
private:
    HttpServer();
    ~HttpServer();
    bool InitServer(const char *ipAddr, const unsigned short int portId,  const unsigned int backlog);
    bool InitEpollFd(const int epollSize);
    static bool InitPipeFd();
    bool RegisterServerReadEvent();
    bool RegisterPipeReadEvent();
    bool RegisterHandleSignal(const int signalId);
    static void WriteSignalToPipeFd(int signalId);
    void EventLoop(const int epollSize);
    void HandleServerReadEvent();
    void HandleClientReadEvent(const int client);
    void DelClient(const int client);
    void HandlePipeReadEvent();
    void HandleWriteEvent(const int client);
    void HandleClientExpire();
    void clear();
    static void ClosePipefd();
    static void ProcessReq(void *arg);
private:
    int m_server { -1 }; // 记录socket服务器套接字，初始化为-1是无效值
    int m_efd { -1 };
    bool m_checkClientExpire { false };
    static int m_pipefd[PIPE_FD_NUM];
    std::string m_sourceDir;
    std::map<int, HttpProcessor*> m_fdAndProcessorMap; // 客户端套接字和处理对象的映射
    ClientExpireMinHeap m_clientExpireMinHeap;
    ThreadPool<HttpReqProcessArg> m_threadPool;
};

#endif