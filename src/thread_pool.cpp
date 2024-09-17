#include "thread_pool.h"
#include <stdio.h>

template <class T>
ThreadPool<T>::ThreadPool(const unsigned int poolNum) : m_threadNum(poolNum)
{}

template <class T>
ThreadPool<T>::~ThreadPool()
{
    Clear();
    m_stop = true; // 停止子线程的任务
}

template <class T>
void ThreadPool<T>::Clear()
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

template <class T>
bool ThreadPool<T>::Init()
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

template <class T>
bool ThreadPool<T>::AddTask(const Task<T> &task)
{
    if (pthread_mutex_lock(&m_mutex) != 0) {
        return false;
    }

    if (m_queue.size() == m_queue.max_size()) {
        (void)pthread_mutex_unlock(&m_mutex);
        return false;
    }
    m_queue.push(task);
    (void)pthread_mutex_unlock(&m_mutex);

    if (sem_post(&m_sem) != 0) {
        return false;
    }

    return true;
}

template <class T>
void *ThreadPool<T>::ThreadFunction(void *arg)
{
    ThreadPool *pool = reinterpret_cast<ThreadPool *>(arg);
    pool->Run();
    return nullptr;
}

template <class T>
void ThreadPool<T>::Run()
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
