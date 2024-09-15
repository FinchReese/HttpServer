#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include "http_server.h"

HttpServer::HttpServer()
{}

HttpServer::~HttpServer()
{
    clear();
}

HttpServer &HttpServer::GetInstance()
{
    static HttpServer httpServer;
    return httpServer;
}

void HttpServer::Run(const char *ipAddr, const unsigned short int portId,  const unsigned int backlog,
    const int epollSize, const char *sourceDir)
{
    if (InitServer(ipAddr, portId, backlog) == false) {
        return;
    }

    if (InitEpollFd(epollSize) == false) {
        if (m_server != -1) {
            close(m_server);
            m_server = -1;
        }
        return;
    }

    if (RegisterServerReadEvent() == false) {
        clear();
        return;
    }
    m_sourceDir = sourceDir;
    EventLoop(epollSize);
    clear();
}

bool HttpServer::InitServer(const char *ipAddr, const unsigned short int portId,  const unsigned int backlog)
{
    if (m_server != -1) {
        printf("ERROR  Server alreadly exists.\n");
        return false;
    }

    m_server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_server == -1) {
        printf("ERROR  Create socket fail.\n");
        return false;
    }

    if (ipAddr == nullptr) {
        close(m_server);
        m_server = -1;
        printf("ERROR  ipAddr is null.\n");
        return false;
    }
    in_addr_t ipNum = inet_addr(ipAddr);
    if (ipNum == INADDR_NONE) {
        close(m_server);
        m_server = -1;       
        printf("ERROR  Invalid ip address: %s.\n", ipAddr);
        return false;
    }

    struct sockaddr_in server_addr = { 0 };
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = ipNum;
    server_addr.sin_port = htons(portId);
    if (bind(m_server, reinterpret_cast<struct sockaddr *>(&server_addr), sizeof(server_addr)) == -1) {
        close(m_server);
        m_server = -1;
        printf("ERROR  server bind fail: %s:%hu.\n", ipAddr, portId);
        return false;
    }

    if (listen(m_server, backlog) == -1) {
        close(m_server);
        m_server = -1;
        printf("ERROR  server listen fail.\n");
        return false;
    }
    printf("EVENT server listen: %s:%hu.\n", ipAddr, portId);
    return true;
}

bool HttpServer::InitEpollFd(const int epollSize)
{
    if (m_efd != -1) {
        printf("ERROR  Epoll alreadly exists.\n");
        return false;
    }

    m_efd = epoll_create(epollSize);
    if (m_efd == -1) {
        printf("ERROR  epoll_create fail.\n");
        return false;
    }

    return true;
}

bool HttpServer::RegisterServerReadEvent()
{
    struct epoll_event serverEvent = { 0 };
    serverEvent.events = EPOLLIN;
    serverEvent.data.fd = m_server;
    int ret = epoll_ctl(m_efd, EPOLL_CTL_ADD, m_server, &serverEvent);
    if (ret == -1) {
        printf("ERROR  Register server read event fail.\n");
        return false;
    }

    return true;
}

void HttpServer::EventLoop(const int epollSize)
{
    struct epoll_event *events = new struct epoll_event[epollSize];
    if (events == nullptr) {
        printf("ERROR  Allooc memory fail.\n");
        return;
    }

    bool stopFlag = false;
    while (!stopFlag) {
        int ret = epoll_wait(m_efd, events, epollSize, -1);
        if (ret == -1) {
            printf("ERROR  epoll_wait fail, errno = %d.\n", errno);
            if (errno == EINTR) {
                continue;
            } else {
                delete[] events;
                return;
            }
        }
        for (unsigned int i = 0; i < static_cast<unsigned int>(ret); ++i) {
            int socket = events[i].data.fd;
            if (events[i].events & EPOLLIN) {
                if (socket == m_server) {
                    HandleServerReadEvent();
                } else {
                    HandleClientReadEvent(socket);
                }
            } else if (events[i].events & EPOLLOUT) {
                HandleWriteEvent(socket);
            }

        }
    }

    delete []events;
    return;
}

void HttpServer::HandleServerReadEvent()
{
    struct sockaddr_in clientAddr = { 0 };
    socklen_t clientAddrLen = sizeof(clientAddr);
    int client = accept(m_server, reinterpret_cast<struct sockaddr *>(&clientAddr), &clientAddrLen);
    if (client == -1) {
        printf("ERROR  accept fail.\n");
        return;
    }
    printf("EVENT  new connect: client[%d] with %s:%hu.\n", client, inet_ntoa(clientAddr.sin_addr),
        ntohs(clientAddr.sin_port));

    // 注册客户端的监听读事件
    struct epoll_event clientEvent = { 0 };
    clientEvent.events = EPOLLIN;
    clientEvent.data.fd = client;
    int ret = epoll_ctl(m_efd, EPOLL_CTL_ADD, client, &clientEvent);
    if (ret == -1) {
        printf("ERROR  epoll_ctl fail.\n");
        close(client);
        return;
    }
    HttpProcessor *httpProcessor = new HttpProcessor(client, m_sourceDir);
    if (httpProcessor == nullptr) {
        printf("ERROR  Create HttpProcessor fail.\n");
        epoll_ctl(m_efd, EPOLL_CTL_DEL, client, NULL);
        close(client);
        return;
    }
    m_fdAndProcessorMap[client] = httpProcessor;
}

void HttpServer::HandleClientReadEvent(const int client)
{
    std::map<int, HttpProcessor*>::iterator iter = m_fdAndProcessorMap.find(client);
    if (iter == m_fdAndProcessorMap.end()) {
        printf("ERROR client[%d] not match processer.\n", client);
        return;
    }
    HttpProcessor *httpProcessor = iter->second;
    bool ret = httpProcessor->Read();
    if (!ret) {
        epoll_ctl(m_efd, EPOLL_CTL_DEL, client, NULL);
        close(client);
        m_fdAndProcessorMap.erase(iter);
        return;
    }
    ret = httpProcessor->ProcessReadEvent();
    if (!ret) {
        epoll_ctl(m_efd, EPOLL_CTL_DEL, client, NULL);
        close(client);
        m_fdAndProcessorMap.erase(iter);
        return;
    }
    // 注册客户端的监听写事件
    struct epoll_event clientEvent = { 0 };
    clientEvent.events = EPOLLOUT;
    clientEvent.data.fd = client;
    int res = epoll_ctl(m_efd, EPOLL_CTL_MOD, client, &clientEvent);
    if (res == -1) {
        printf("ERROR Register write event fail.\n");
        epoll_ctl(m_efd, EPOLL_CTL_DEL, client, NULL);
        close(client);
        m_fdAndProcessorMap.erase(iter);
        return;
    }
}

void HttpServer::HandleWriteEvent(const int client)
{
    std::map<int, HttpProcessor*>::iterator iter = m_fdAndProcessorMap.find(client);
    if (iter == m_fdAndProcessorMap.end()) {
        printf("ERROR client[%d] not match processer.\n", client);
        return;
    }
    HttpProcessor *httpProcessor = iter->second;
    SendResponseReturnCode ret = httpProcessor->Write();
    printf("EVENT  Write ret:%u.\n", ret);
    switch (ret) {
        case SEND_RESPONSE_RETURN_CODE_AGAIN: {
            // 写缓冲区满不需要做额外处理，等待写缓冲区有空间即可
            break;
        }
        case SEND_RESPONSE_RETURN_CODE_NEXT: {
            // 注册客户端的监听读事件
            struct epoll_event clientEvent = { 0 };
            clientEvent.events = EPOLLIN;
            clientEvent.data.fd = client;
            int res = epoll_ctl(m_efd, EPOLL_CTL_MOD, client, &clientEvent);
            if (res == -1) {
                printf("ERROR  register in event fail.\n");
                close(client);
                epoll_ctl(m_efd, EPOLL_CTL_DEL, client, NULL);
                m_fdAndProcessorMap.erase(iter);
            }
            break;
        }
        default: {
            close(client);
            epoll_ctl(m_efd, EPOLL_CTL_DEL, client, NULL);
            m_fdAndProcessorMap.erase(iter);
            break;            
        }
    }
}

void HttpServer::clear()
{
    if (m_server != -1) {
        close(m_server);
        m_server = -1;
    }
    if (m_efd != -1) {
        close(m_efd);
        m_efd = -1;
    }
    for (auto iter = m_fdAndProcessorMap.begin(); iter != m_fdAndProcessorMap.end(); ++iter) {
        close(iter->first);
        delete iter->second;
    }
}