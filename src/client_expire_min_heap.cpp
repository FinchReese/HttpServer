#include "client_expire_min_heap.h"

const unsigned int MAX_U32 = 0xFFFFFFFF;
const unsigned int ROOT_NODE_INDEX = 0; // 根节点下标为0

ClientExpireMinHeap::ClientExpireMinHeap()
{}

ClientExpireMinHeap::~ClientExpireMinHeap()
{
    if (m_heap != nullptr) {
        delete []m_heap;
        m_heap = nullptr;
    }
}

bool ClientExpireMinHeap::Init(unsigned int capacity)
{
    m_currentSize = 0;
    m_capacity = 0;
    if (m_heap != nullptr) {
        delete []m_heap;
        m_heap = nullptr;
    }
    if (capacity == 0) {
        return false;
    }

    m_heap = new ClientExpire[capacity];
    if (m_heap == nullptr) {
        return false;
    }
    m_capacity = capacity;
    return true;
}

bool ClientExpireMinHeap::Init(unsigned int capacity, ClientExpire *array, const unsigned int arraySize)
{
    if (Init(capacity) == false) {
        return false;
    }
    if (arraySize > capacity || array == nullptr) {
        return false;
    }

    // 初始化最小堆
    for (unsigned int i = 0; i < arraySize; ++i) {
        if (Push(array[i]) == false) {
            return false;
        }
    }
    m_currentSize = arraySize;
    return true;
}

void ClientExpireMinHeap::SiftDown(const unsigned int startIdx)
{
    if (startIdx >= m_currentSize) {
        return;
    }

    unsigned int currentIdx = startIdx; // 记录目标节点当前位置下标
    unsigned int childIdx = currentIdx * 2 + 1; // 记录目标节点的较小子节点位置下标，一开始先指向左节点
    const ClientExpire &value = m_heap[startIdx];

    while (childIdx < m_currentSize) {
        if (childIdx < m_currentSize - 1) {
            if (m_heap[childIdx + 1].expire < m_heap[childIdx].expire) {
                childIdx++;
            }
        }
        if (m_heap[childIdx].expire >= m_heap[currentIdx].expire) {
            break;
        }
        m_heap[currentIdx] = m_heap[childIdx];
        m_socketAndHeapIdxMap[m_heap[childIdx].clientFd] = currentIdx;
        currentIdx = childIdx;
        childIdx = currentIdx * 2 + 1; // 记录目标节点的较小子节点位置下标，一开始先指向左节点
    }
    m_heap[currentIdx] = value;
    m_socketAndHeapIdxMap[value.clientFd] = currentIdx;
}

void ClientExpireMinHeap::SiftUp(const unsigned int startIdx)
{
    if (startIdx == ROOT_NODE_INDEX) { // 目标节点是根节点不需要调整
        return;
    }

    unsigned int currentIdx = startIdx; // 记录节点当前位置下标
    unsigned int parentIdx; // 当前节点父节点下标
    ClientExpire value = m_heap[startIdx];
    while (currentIdx > 0) {
        parentIdx = (currentIdx - 1) / 2;
        if (m_heap[parentIdx].expire <= value.expire) {
            break;
        }
        m_heap[currentIdx] = m_heap[parentIdx];
        m_socketAndHeapIdxMap[m_heap[parentIdx].clientFd] = currentIdx;
        currentIdx = parentIdx;
    }
    m_heap[currentIdx] = value;
    m_socketAndHeapIdxMap[value.clientFd] = currentIdx;
}

bool ClientExpireMinHeap::Resize()
{
    unsigned int newCapacity;
    if (m_capacity > MAX_U32 / 2) {
        newCapacity = MAX_U32;
    } else if (m_capacity == 0) {
        newCapacity = 1; // 最小堆空间设置为1
    } else {
        newCapacity = m_capacity * 2;
    }

    ClientExpire *newHeap = new ClientExpire[newCapacity];
    if (newHeap == nullptr) {
        return false;
    }
    m_capacity = newCapacity;

    if (m_heap != nullptr) {
        for (unsigned int i = 0; i < m_currentSize; ++i) {
            newHeap[i] = m_heap[i];
        }
        delete []m_heap;
    }

    m_heap = newHeap;
    return true;
}

bool ClientExpireMinHeap::Push(const ClientExpire &node)
{
    if (m_currentSize == m_capacity) {
        if (Resize() == false) {
            return false;
        }
    }

    std::pair<int, unsigned int> socketAndHeapIdxPair(node.clientFd, m_currentSize);
    auto ret = m_socketAndHeapIdxMap.insert(socketAndHeapIdxPair);
    if (!ret.second) {
        return false;
    }
    m_heap[m_currentSize] = node;
    SiftUp(m_currentSize);
    ++m_currentSize;
    return true;
}

bool ClientExpireMinHeap::Pop(ClientExpire &node)
{
    if (m_currentSize == 0) {
        return false;
    }

    node = m_heap[ROOT_NODE_INDEX];
    m_socketAndHeapIdxMap.erase(node.clientFd);
    m_heap[ROOT_NODE_INDEX] = m_heap[m_currentSize - 1]; // 将最后一个元素移到根节点
    m_socketAndHeapIdxMap[m_heap[ROOT_NODE_INDEX].clientFd] = ROOT_NODE_INDEX;
    m_currentSize--;
    SiftDown(ROOT_NODE_INDEX);
    return true;
}

bool ClientExpireMinHeap::Top(ClientExpire &node)
{
    if (m_currentSize == 0) {
        return false;
    }

    node = m_heap[ROOT_NODE_INDEX];
    return true;
}

bool ClientExpireMinHeap::Modify(const ClientExpire &node)
{
    int clientFd = node.clientFd;
    auto iter = m_socketAndHeapIdxMap.find(clientFd);
    if (iter == m_socketAndHeapIdxMap.end()) {
        printf("ERROR client[%d] not find.\n", clientFd);
        return false;
    }
    unsigned int heapIdx = iter->second;
    time_t oldExpire = m_heap[heapIdx].expire;
    m_heap[heapIdx].expire = node.expire;
    // 调整位置
    if (node.expire < oldExpire) {
        SiftUp(heapIdx);
    } else if (node.expire > oldExpire) {
        SiftDown(heapIdx);
    }
    return true;
}

bool ClientExpireMinHeap::Delete(const int clientFd)
{
    auto iter = m_socketAndHeapIdxMap.find(clientFd);
    if (iter == m_socketAndHeapIdxMap.end()) {
        printf("ERROR client[%d] not find.\n", clientFd);
        return false;
    }
    unsigned int heapIdx = iter->second;
    time_t oldExpire = m_heap[heapIdx].expire; 
    m_heap[heapIdx] = m_heap[m_currentSize - 1];
    m_socketAndHeapIdxMap[m_heap[heapIdx].clientFd] = heapIdx;
    m_currentSize--;
    // 调整位置
    if (m_heap[heapIdx].expire < oldExpire) {
        SiftUp(heapIdx);
    } else if (m_heap[heapIdx] > oldExpire) {
        SiftDown(heapIdx);
    }
    return true;
}