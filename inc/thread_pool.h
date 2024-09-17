#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>
#include <semaphore.h>
#include <queue>

typedef void *(TaskFunction)(void *);

template <class T>
struct Task {
    TaskFunction function;
    T arg;
};

template <class T>
class ThreadPool {
public:
    ThreadPool(const unsigned int threadNum);
    ~ThreadPool();
    bool Init();
    bool AddTask(const Task<T> &task);
private:
    static void *ThreadFunction(void *arg);
    void Run();
private:
    unsigned int m_threadNum;
    pthread_t *m_threadPool { nullptr };
    bool m_stop { false };
    bool m_initMutex { false };
    bool m_initSem { false };
    std::queue<Task<T>> m_queue;
    pthread_mutex_t m_mutex;
    sem_t m_sem;
};

#endif