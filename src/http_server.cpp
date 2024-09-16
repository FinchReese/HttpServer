#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include "http_server.h"

enum PipeFdIdx {
    PIPE_WRITE_FD_INDEX = 0, // 用于写的套接字
    PIPE_READ_FD_INDEX = 1, // 用于读的套接字
};

const unsigned int CLIENT_EXPIRE_MIN_HEAP_DEFAULT_SIZE = 10; // 客户端过期时间最小堆默认大小为10
const unsigned int TIMER_INTERVAL = 5; // 定时器间隔设置为5秒
const unsigned int CLIENT_EXPIRE_INTERVAL = TIMER_INTERVAL * 3; // 客户端过期时间间隔设置为3个定时器间隔

int HttpServer::m_pipefd[PIPE_FD_NUM] { -1, -1 };

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

    if (InitPipeFd() == false) {
        clear();
        return;
    }

    if (m_clientExpireMinHeap.Init(CLIENT_EXPIRE_MIN_HEAP_DEFAULT_SIZE) == false) {
        clear();
        return;        
    }

    if (RegisterServerReadEvent() == false) {
        clear();
        return;
    }
    if (RegisterPipeReadEvent() == false) {
        clear();
        return;
    }
    if (RegisterHandleSignal(SIGALRM) == false) {
        clear();
        return;
    }
    alarm(TIMER_INTERVAL); // 开启定时器
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

bool HttpServer::InitPipeFd()
{
    ClosePipefd();
    int ret = socketpair(AF_UNIX, SOCK_STREAM, 0, m_pipefd);
    if (ret == -1) {
        printf("ERROR  socketpair fail.\n");
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

bool HttpServer::RegisterPipeReadEvent()
{
    struct epoll_event pipeEvent = { 0 };
    pipeEvent.events = EPOLLIN;
    pipeEvent.data.fd = m_pipefd[PIPE_READ_FD_INDEX];
    int ret = epoll_ctl(m_efd, EPOLL_CTL_ADD, m_pipefd[PIPE_READ_FD_INDEX], &pipeEvent);
    if (ret == -1) {
        printf("ERROR  Register pipe read event fail.\n");
        return false;
    }
    return true;
}

bool HttpServer::RegisterHandleSignal(const int signalId)
{
    struct sigaction sa = { 0 };
    sa.sa_handler = WriteSignalToPipeFd;
    sigfillset(&sa.sa_mask);
    sa.sa_flags |= SA_RESTART;
    if (sigaction(signalId, &sa, NULL) == -1) {
        printf("ERROR sigaction fail, signalId = %d.\n", signalId);
        return false;
    }
    return true;
}

void HttpServer::WriteSignalToPipeFd(int signalId)
{
    int tmpErrno = errno;
    ssize_t ret = write(m_pipefd[PIPE_WRITE_FD_INDEX], &signalId, sizeof(signalId));
    printf("DEBUG  WriteSignalToPipeFd signalId=%d, ret=%ld, tmpErrno=%d, errno=%d\n",
        signalId, ret, tmpErrno, errno);
    errno = tmpErrno;
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
                } else if (socket == m_pipefd[PIPE_READ_FD_INDEX]) {
                    HandlePipeReadEvent();
                } else {
                    HandleClientReadEvent(socket);
                }
            } else if (events[i].events & EPOLLOUT) {
                HandleWriteEvent(socket);
            }
        }
        if (m_checkClientExpire) {
            HandleClientExpire();
            m_checkClientExpire = false;
            alarm(TIMER_INTERVAL); // 重启定时器
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
    // 创建客户端的请求处理器
    HttpProcessor *httpProcessor = new HttpProcessor(client, m_sourceDir);
    if (httpProcessor == nullptr) {
        printf("ERROR  Create HttpProcessor fail.\n");
        epoll_ctl(m_efd, EPOLL_CTL_DEL, client, NULL);
        close(client);
        return;
    }
    m_fdAndProcessorMap[client] = httpProcessor;
    // 将客户端注册到过期时间最小堆
    time_t curSec = time(NULL);
    ClientExpire clientExpire = { .clientFd = client, .expire = curSec + CLIENT_EXPIRE_INTERVAL };
    if (m_clientExpireMinHeap.Push(clientExpire) == false) {
        delete httpProcessor;
        m_fdAndProcessorMap.erase(client);
        epoll_ctl(m_efd, EPOLL_CTL_DEL, client, NULL);
        close(client);
        return;
    }
    printf("EVENT  new connect: client[%d] with %s:%hu.\n", client, inet_ntoa(clientAddr.sin_addr),
        ntohs(clientAddr.sin_port));
}

void HttpServer::HandlePipeReadEvent()
{
    int signalid = -1;
    ssize_t readBytes = read(m_pipefd[PIPE_READ_FD_INDEX], &signalid, sizeof(signalid));
    if (readBytes != sizeof(signalid)) {
        return;
    }
    printf("EVENT Recv Signal %d\n", signalid);
    if (signalid == SIGALRM) {
        m_checkClientExpire = true;
    }
}

void HttpServer::HandleClientReadEvent(const int client)
{
    std::map<int, HttpProcessor*>::iterator iter = m_fdAndProcessorMap.find(client);
    if (iter == m_fdAndProcessorMap.end()) {
        printf("ERROR client[%d] not match processer.\n", client);
        return;
    }
    HttpProcessor *httpProcessor = iter->second;
    RecvRequestReturnCode returnCode = httpProcessor->Read();
    switch (returnCode) {
        case RECV_REQUEST_RETURN_CODE_AGAIN: { // 读缓冲区为空等待下一次读事件
            break;
        }
        case RECV_REQUEST_RETURN_CODE_ERROR: {  // 读消息出错断开连接
            DelClient(client);
            break;
        }
        case RECV_REQUEST_RETURN_CODE_SUCCESS: { // 读消息成功处理请求
            bool ret = httpProcessor->ProcessReadEvent();
            if (!ret) {
                DelClient(client);
                break;
            }
            // 注册客户端的监听写事件
            struct epoll_event clientEvent = { 0 };
            clientEvent.events = EPOLLOUT;
            clientEvent.data.fd = client;
            int res = epoll_ctl(m_efd, EPOLL_CTL_MOD, client, &clientEvent);
            if (res == -1) {
                printf("ERROR Register write event fail.\n");
                DelClient(client);
            }
            // 更新客户端的过期时间
            time_t curSec = time(NULL);
            ClientExpire clientExpire = { .clientFd = client, .expire = curSec + CLIENT_EXPIRE_INTERVAL };
            m_clientExpireMinHeap.Modify(clientExpire);
            break;
        }
        default: { // 不会有其他响应码，编码规范要求要有default分支
            break;
        }
    }
}

void HttpServer::DelClient(const int client)
{
    epoll_ctl(m_efd, EPOLL_CTL_DEL, client, NULL);
    close(client);
    std::map<int, HttpProcessor*>::iterator iter = m_fdAndProcessorMap.find(client);
    if (iter != m_fdAndProcessorMap.end()) {
        m_fdAndProcessorMap.erase(iter);
    }
    m_clientExpireMinHeap.Delete(client);
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

void HttpServer::HandleClientExpire()
{
    time_t curSec = time(NULL);
    ClientExpire clientExpire = { 0 };
    bool ret;
    do {
        ret = m_clientExpireMinHeap.Top(clientExpire);
        if (!ret) {
            break;
        }
        if (clientExpire.expire > curSec) {
            break;
        }
        DelClient(clientExpire.clientFd);
    } while (true);
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
    ClosePipefd();
}

void HttpServer::ClosePipefd()
{
    if (m_pipefd[PIPE_WRITE_FD_INDEX] != -1) {
        close(m_pipefd[PIPE_WRITE_FD_INDEX]);
        m_pipefd[PIPE_WRITE_FD_INDEX] = -1;
    }
    if (m_pipefd[PIPE_READ_FD_INDEX] != -1) {
        close(m_pipefd[PIPE_READ_FD_INDEX]);
        m_pipefd[PIPE_READ_FD_INDEX] = -1;
    }
}