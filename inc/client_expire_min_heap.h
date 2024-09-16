#ifndef CLIENT_EXPIRE_MIN_HEAP_H
#define CLIENT_EXPIRE_MIN_HEAP_H

#include <time.h>
#include <map>

typedef struct {
    int clientFd;
    time_t expire;
} ClientExpire;

class ClientExpireMinHeap {
public:
    ClientExpireMinHeap();
    ~ClientExpireMinHeap();
    bool Init(unsigned int heapSize);
    bool Init(unsigned int capacity, ClientExpire *array, const unsigned int arraySize);
    bool Push(const ClientExpire &node);
    bool Pop(ClientExpire &node);
    bool Top(ClientExpire &node);
    bool Modify(const ClientExpire &node);
    bool Delete(const int clientFd);
private:
    void SiftDown(const unsigned int startIdx);
    void SiftUp(const unsigned int startIdx);
    bool Resize();
private:
    unsigned int m_capacity { 0 };
    unsigned int m_currentSize { 0 };
    ClientExpire *m_heap { nullptr };
    std::map<int, unsigned int> m_socketAndHeapIdxMap; // 套接字id和客户端超时对应在最小堆位置的映射

};
#endif