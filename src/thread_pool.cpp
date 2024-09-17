#include "thread_pool.h"

ThreadPool::ThreadPool(const unsigned int poolNum) : m_threadNum(poolNum)
{}

ThreadPool::~ThreadPool()
{
    Clear();
}

void ThreadPool::Clear()
{
    // 销毁互斥锁
    if (m_initMutex) {
        (void)pthread_mutex_destroy(&m_mutex);
        m_initMutex = false;
    }
    if (m_initSem) {
        // 销毁信号量
        (void)sem_destropy(&m_sem);
        m_initSem = false;
    }
    // 销毁线程池
    if (m_threadPool != nullptr) {
        delete []m_threadPool;
        m_threadPool = nullptr;
    }
}

bool ThreadPool::Init()
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
    int ret = sem_init(&m_sem, 0, 0);
    if (ret != 0) {
        printf("ERROR sem_init fail, ret = %d\n", ret);
        Clear();
        return false;
    }
    m_initSem = true;
    // 初始化线程池
    m_threadPool = new thread_t[m_threadNum];
    if (m_threadPool == nullptr) {
        Clear();
        return false;
    }
    for (unsigned int i = 0; i < m_threadNum; ++i) {
        if(pthread_create(&m_threadPool[i], nullptr, ThreadPool::ThreadFunction, this) != 0) {
            Clear();
            return false;
        }
        if (pthread_detach() != 0) {
            Clear();
            return false;
        }
    }

    return true;
}

bool ThreadPool::AddTask(const Task &task)
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

void *ThreadPool::ThreadFunction(void *argv)
{
    ThreadPool *pool = reinterpret_cast<ThreadPool *>(argv);
    pool->Run();
    return pool;
}

void *ThreadPool::Run(void *argv)
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

        Task task = m_queue.front();
        m_queue.pop();
        (void)pthread_mutex_unlock(&m_mutex);
        task.function(task.argv);
    }

}
