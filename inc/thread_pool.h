#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>
#include <semaphore.h>
#include <queue>
#include <stdio.h>

typedef void (*TaskFunction)(void *);

template <class T>
struct Task {
    TaskFunction function;
    T arg;
};

template <class T>
class ThreadPool {
public:
    ThreadPool(const unsigned int threadNum) : m_threadNum(threadNum) {}
    ~ThreadPool()
    {
        Clear();
        m_stop = true; // 停止子线程的任务
    }
    bool Init()
    {
        if (m_initMutex) { // 代码逻辑可以保证m_initMutex为true，已经成功执行过Init，不需要重复执行
            return true;
        }
        int ret = pthread_mutex_init(&m_mutex, nullptr);
        if (ret != 0) {
            printf("ERROR pthread_mutex_init fail, ret = %d\n", ret);
            return false;
        }
        m_initMutex = true;
        // 初始化信号量
        ret = sem_init(&m_sem, 0, 0);
        if (ret != 0) {
            printf("ERROR sem_init fail, ret = %d\n", ret);
            Clear();
            return false;
        }
        m_initSem = true;
        // 初始化线程池
        m_threadPool = new pthread_t[m_threadNum];
        if (m_threadPool == nullptr) {
            Clear();
            return false;
        }
        for (unsigned int i = 0; i < m_threadNum; ++i) {
            if(pthread_create(&m_threadPool[i], nullptr, ThreadPool::ThreadFunction, this) != 0) {
                Clear();
                return false;
            }
            if (pthread_detach(m_threadPool[i]) != 0) {
                Clear();
                return false;
            }
        }

        return true;
    }
    bool AddTask(const Task<T> &task)
    {
        if (pthread_mutex_lock(&m_mutex) != 0) {
            return false;
        }
        m_queue.push(task);
        (void)pthread_mutex_unlock(&m_mutex);

        if (sem_post(&m_sem) != 0) {
            return false;
        }

        return true;
    }
private:
    void Clear()
    {
        // 销毁互斥锁
        if (m_initMutex) {
            (void)pthread_mutex_destroy(&m_mutex);
            m_initMutex = false;
        }
        if (m_initSem) {
            // 销毁信号量
            (void)sem_destroy(&m_sem);
            m_initSem = false;
        }
        // 销毁线程池
        if (m_threadPool != nullptr) {
            delete []m_threadPool;
            m_threadPool = nullptr;
        }
    }

    static void *ThreadFunction(void *arg)
    {
        ThreadPool *pool = reinterpret_cast<ThreadPool *>(arg);
        pool->Run();
        return nullptr;
    }
    void Run()
    {
        while (m_stop == false) {
            if (sem_wait(&m_sem) != 0) {
                continue;
            }
            if (pthread_mutex_lock(&m_mutex) != 0) {
                continue;
            }

            if (m_queue.empty()) {
                (void)pthread_mutex_unlock(&m_mutex);
                continue;
            }

            Task<T> task = m_queue.front();
            m_queue.pop();
            (void)pthread_mutex_unlock(&m_mutex);
            task.function(&task.arg);
        }
    }
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