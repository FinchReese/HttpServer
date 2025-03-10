#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "http_processor.h"

const char *WHITE_SPACE_CHARS = " \t";
const char *GET_METHOD_STR = "GET";
const char *URL_HTTP_PREFIX = "http://";
const char URL_SPLIT_CHAR = '/';
const char HEAD_FIELD_SPLIT_CHAR = ':';
const char END_CHAR = '\0'; // 结束符
const char *CONTENT_LENGTH_KEY_NAME = "Content-Length";
const char *CONNECTION_KEY_NAME = "Connection";
const char *KEEP_ALIVE_VALUE = "keep-alive";
const char *CLOSE_ALIVE_VALUE = "close";
const char *OK_TITLE = "OK";
const char *BAD_REQUEST_TITLE = "Bad Request";
const char *BAD_REQUEST_CONTENT = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char *FORBIDDEN_TITLE = "Forbidden";
const char *FORBIDDEN_CONTENT = "You don't have permission to get file from this server.\n";
const char *NOT_FOUND_TITLE = "Not Found";
const char *NOT_FOUND_CONTENT = "The request file was not found on this server.\n";
const char *INTERNAL_SERVER_ERROR_TITLE = "Internal Server Error";
const char *INTERNAL_SERVER_ERROR_CONTENT = "There was an unusual problem serving the requested file.\n";
const unsigned int MAX_FILE_NAME_LEN = 200;

const StatusInfo ERROR_STATUS_INFO_LIST[] = {
    { RESPONSE_STATUS_CODE_BAD_REQUEST, BAD_REQUEST_TITLE, BAD_REQUEST_CONTENT },
    { RESPONSE_STATUS_CODE_FORBIDDEN, FORBIDDEN_TITLE, FORBIDDEN_CONTENT },
    { RESPONSE_STATUS_CODE_NOT_FOUND, NOT_FOUND_TITLE, NOT_FOUND_CONTENT },
    { RESPONSE_STATUS_CODE_INTERNAL_SERVER_ERROR, INTERNAL_SERVER_ERROR_TITLE, INTERNAL_SERVER_ERROR_CONTENT },
};
const unsigned int ERROR_STATUS_INFO_LIST_SIZE = sizeof(ERROR_STATUS_INFO_LIST) / sizeof(ERROR_STATUS_INFO_LIST[0]);

HttpProcessor::HttpProcessor(const int socketId, const std::string &sourceDir) : m_socketId(socketId), m_sourceDir(sourceDir)
{}

HttpProcessor::~HttpProcessor()
{}

bool HttpProcessor::ProcessReadEvent()
{
    ParseRequestReturnCode ret = ParseRequest();
    printf("EVENT ParseRequest ret = %u\n", ret);
    return Response(ret);
}

RecvRequestReturnCode HttpProcessor::Read()
{
    ssize_t readSize = read(m_socketId, m_request + m_currentRequestSize, MAX_READ_BUFF_LEN - m_currentRequestSize);
    if (readSize <= 0) {
        if (errno == EAGAIN) {
            return RECV_REQUEST_RETURN_CODE_AGAIN;
        }
        printf("ERROR read fail, socket id = %d\n", m_socketId);
        return RECV_REQUEST_RETURN_CODE_ERROR;
    }
    m_currentRequestSize += readSize;

    struct sockaddr_in clientAddr = { 0 };
    socklen_t clientAddrLen = sizeof(clientAddr);
    if (getpeername(m_socketId, reinterpret_cast<struct sockaddr *>(&clientAddr), &clientAddrLen) == -1) {
        printf("\nDEBUG  client[%u] recv msg:\n%s\n", m_socketId, m_request);
    } else {
    printf("\nDEBUG  client[%u] %s:%hu recv msg:\n%s\n", m_socketId, inet_ntoa(clientAddr.sin_addr),
        ntohs(clientAddr.sin_port), m_request);
    }

    return RECV_REQUEST_RETURN_CODE_SUCCESS;
}

SendResponseReturnCode HttpProcessor::Write()
{
    if (m_leftRespSize == 0) {
        printf("ERROR No content need to send.\n");
        return SEND_RESPONSE_RETURN_CODE_ERROR;
    }
    struct sockaddr_in clientAddr = { 0 };
    socklen_t clientAddrLen = sizeof(clientAddr);
    if (getpeername(m_socketId, reinterpret_cast<struct sockaddr *>(&clientAddr), &clientAddrLen) == -1) {
        printf("DEBUG client[%u] msg to send:\n", m_socketId);
    } else {
    printf("DEBUG client[%u] %s:%hu msg to send:\n", m_socketId, inet_ntoa(clientAddr.sin_addr),
        ntohs(clientAddr.sin_port));
    }
    printf("%s", m_writeBuff);
    if (m_cnt == VECTOR_COUNT) {
        printf("%s", m_fileAddr);
    }
    printf("\n");
    ssize_t ret;
    while (true) {
        ret = writev(m_socketId, m_iov, m_cnt);
        // 发送回复消息异常
        if (ret == -1) {
            if (errno == EAGAIN) {
                return SEND_RESPONSE_RETURN_CODE_AGAIN;
            }
            munmap(m_fileAddr, m_fileSize);
            m_fileAddr = nullptr;
            m_fileSize = 0;
            return SEND_RESPONSE_RETURN_CODE_ERROR;
        }
        unsigned int writeSize = static_cast<unsigned int >(ret);
        if (writeSize > m_leftRespSize) {
            return SEND_RESPONSE_RETURN_CODE_ERROR;
        }
        m_leftRespSize -= writeSize;
        // 发送回复消息完成
        if (m_leftRespSize == 0) {
            munmap(m_fileAddr, m_fileSize);
            m_fileAddr = nullptr;
            m_fileSize = 0;
            if (m_keepAlive) {
                Init();
                return SEND_RESPONSE_RETURN_CODE_NEXT;
            }
            return SEND_RESPONSE_RETURN_CODE_FINISH;
        }
        // 更新向量信息
        if (writeSize >= m_iov[STATUS_LINE_AND_HEAD_FIELD_VECTOR_INDEX].iov_len) {
            unsigned int contentVectorOffset = writeSize - m_iov[STATUS_LINE_AND_HEAD_FIELD_VECTOR_INDEX].iov_len;
            m_iov[STATUS_LINE_AND_HEAD_FIELD_VECTOR_INDEX].iov_len = 0;
            m_iov[CONTENT_VECTOR_INDEX].iov_base = 
                reinterpret_cast<void *>(reinterpret_cast<char *>(m_iov[CONTENT_VECTOR_INDEX].iov_base) + contentVectorOffset);
            m_iov[CONTENT_VECTOR_INDEX].iov_len -= contentVectorOffset;
        } else {
            m_iov[STATUS_LINE_AND_HEAD_FIELD_VECTOR_INDEX].iov_base =
                reinterpret_cast<void *>(reinterpret_cast<char *>(m_iov[CONTENT_VECTOR_INDEX].iov_base) + writeSize);
            m_iov[STATUS_LINE_AND_HEAD_FIELD_VECTOR_INDEX].iov_len -= writeSize;                
        }
    }
    return SEND_RESPONSE_RETURN_CODE_ERROR;
}

void HttpProcessor::Init()
{
    memset(m_request, 0, sizeof(m_request));
    m_currentRequestSize = 0;
    m_parseStartPos = m_request;
    m_currentIndex = 0;
    m_processState = HTTP_PROCESS_STATE_PARSE_REQUEST_LINE;
    m_method = nullptr;
    m_url = nullptr;
    m_httpVersion = nullptr;
    m_contentLen = 0;
    m_keepAlive = false;
    memset(m_writeBuff, 0, sizeof(m_writeBuff));
    m_writeSize = 0;
    m_fileAddr = nullptr;
    m_fileSize = 0;
    memset(m_iov, 0, sizeof(m_iov));
    m_cnt = 0;
    m_leftRespSize = 0; // 剩余回复字节数
}

ParseRequestReturnCode HttpProcessor::ParseRequest()
{
    ParseRequestReturnCode ret;
    while (true) {
        switch (m_processState) {
            case HTTP_PROCESS_STATE_PARSE_REQUEST_LINE: {
                ret = ParseRequestLine();
                break;
            }
            case HTTP_PROCESS_STATE_PARSE_HEAD_FIELD: {
                ret = ParseHeadFields();
                break;
            }
            case HTTP_PROCESS_STATE_PARSE_REQUEST_BODY: {
                ret = ParseContent();
                break;
            }
            default: {
                printf("ERROR Invalid state:%u.\n", m_processState);
                ret = PARSE_REQUEST_RETURN_CODE_ERROR;
                break;
            }
        }
        if (ret != PARSE_REQUEST_RETURN_CODE_CONTINUE) {
            return ret;
        }
    }
}

ParseRequestReturnCode HttpProcessor::ParseRequestLine()
{
    GetSingleLineState state = GetSingleLine();
    if (state == GET_SINGLE_LINE_ERROR) {
        return PARSE_REQUEST_RETURN_CODE_ERROR;
    }
    if (state == GET_SINGLE_LINE_CONTINUE) {
        return PARSE_REQUEST_RETURN_CODE_WAIT_FOR_READ;
    }
    if (GetField(m_method) == false) {
        printf("ERROR Get method fail.\n");
        return PARSE_REQUEST_RETURN_CODE_ERROR;
    }
    if (strcasecmp(m_method, GET_METHOD_STR) != 0) {
        printf("ERROR Support GET method only.\n");
        return PARSE_REQUEST_RETURN_CODE_ERROR;
    }

    if (GetField(m_url) == false) {
        printf("ERROR Get url fail.\n");
        return PARSE_REQUEST_RETURN_CODE_ERROR;
    }

    if (strncasecmp(m_url, URL_HTTP_PREFIX, strlen(URL_HTTP_PREFIX)) == 0) {
        m_url += strlen(URL_HTTP_PREFIX);
        m_url = strchr(m_url, URL_SPLIT_CHAR);
    }
    if (m_url == nullptr || m_url[0] != URL_SPLIT_CHAR) {
        printf("ERROR Invalid url.\n");
        return PARSE_REQUEST_RETURN_CODE_ERROR;
    }

    m_httpVersion = m_parseStartPos;
    m_parseStartPos += (strlen(m_parseStartPos) + 2); // 2个结束符
    printf("EVENT Req info: %s %s %s\n", m_method, m_url, m_httpVersion);
    m_processState = HTTP_PROCESS_STATE_PARSE_HEAD_FIELD;
    return PARSE_REQUEST_RETURN_CODE_CONTINUE;
}

// 尝试找到"\r\n"，是换行的标记
GetSingleLineState HttpProcessor::GetSingleLine()
{
    if (m_currentIndex >= m_currentRequestSize) {
        return GET_SINGLE_LINE_CONTINUE;
    }

    while (m_currentIndex < m_currentRequestSize) {
        switch (m_request[m_currentIndex]) {
            case '\r': {
                if (m_currentIndex + 1 < m_currentRequestSize) {
                    if (m_request[m_currentIndex + 1] == '\n') {
                        m_request[m_currentIndex++] = END_CHAR ;
                        m_request[m_currentIndex++] = END_CHAR ;
                        return GET_SINGLE_LINE_OK;
                    } else {
                        return GET_SINGLE_LINE_ERROR;
                    }
                } else {
                    return GET_SINGLE_LINE_CONTINUE;
                }
            }
            case '\n': {
                return GET_SINGLE_LINE_ERROR;
            }
            default: {
                m_currentIndex++;
                break;
            }
        }
    }
    return GET_SINGLE_LINE_CONTINUE;
}

bool HttpProcessor::GetField(char *&field)
{
    char *ret = strpbrk(m_parseStartPos, WHITE_SPACE_CHARS);
    if (ret == nullptr) {
        printf("ERROR Can't find white-space char: %s\n", m_parseStartPos);
        return false;
    }
    *ret = END_CHAR ;
    field = m_parseStartPos;
    ret++;
    m_parseStartPos = ret + strspn(ret, WHITE_SPACE_CHARS);
    return true;
}

ParseRequestReturnCode HttpProcessor::ParseHeadFields()
{
    GetSingleLineState state = GetSingleLine();
    if (state == GET_SINGLE_LINE_ERROR) {
        return PARSE_REQUEST_RETURN_CODE_ERROR;
    }
    if (state == GET_SINGLE_LINE_CONTINUE) {
        return PARSE_REQUEST_RETURN_CODE_WAIT_FOR_READ;
    }
    if (*m_parseStartPos == END_CHAR) {
        if (m_contentLen != 0) {
            m_processState = HTTP_PROCESS_STATE_PARSE_REQUEST_BODY;
            return PARSE_REQUEST_RETURN_CODE_CONTINUE;
        }
        return PARSE_REQUEST_RETURN_CODE_FINISH;
    }

    for (auto iter = m_keyNameAndParseFuncMap.begin(); iter != m_keyNameAndParseFuncMap.end(); ++iter) {
        if (strncasecmp(m_parseStartPos, iter->first, strlen(iter->first)) != 0) {
            continue;
        }
        m_parseStartPos += strlen(iter->first);
        if (*m_parseStartPos != HEAD_FIELD_SPLIT_CHAR) {
            printf("ERROR invalid head field:%s.\n", m_parseStartPos);
            return PARSE_REQUEST_RETURN_CODE_ERROR;
        }
        m_parseStartPos += 1; // 跳过':'
        m_parseStartPos += strspn(m_parseStartPos, "\t ");
        (this->*iter->second)();
        m_parseStartPos += (strlen(m_parseStartPos) + 2); // 2表示跳过\r\n
        return PARSE_REQUEST_RETURN_CODE_CONTINUE;
    }
    m_parseStartPos += (strlen(m_parseStartPos) + 2); // 2表示跳过\r\n
    return PARSE_REQUEST_RETURN_CODE_CONTINUE;
}

void HttpProcessor::ParseContentLength()
{
    m_contentLen = atol(m_parseStartPos);
    printf("INFO m_contentLen:%u\n", m_contentLen);
}

void HttpProcessor::ParseConnection()
{
    if (strcasecmp(m_parseStartPos, KEEP_ALIVE_VALUE) == 0) {
        m_keepAlive = true;
    }
    printf("INFO m_keepAlive:%u\n", m_keepAlive);
}

ParseRequestReturnCode HttpProcessor::ParseContent()
{
    unsigned int parseSize = m_parseStartPos - m_request; // 请求体前面信息所占字节数
    // 消息体所占字节数必须小于读缓冲区的剩余空间大小，预留一个结束符
    if (m_contentLen >= sizeof(m_request) - parseSize) {
        printf("ERROR invalid Content-Length, m_contentLen = %u, parseSize = %u\n",
            m_contentLen, parseSize);
        return PARSE_REQUEST_RETURN_CODE_ERROR;
    }
    // 当前读取的消息体字节数是否达到目标消息体字节数
    if (m_currentRequestSize - parseSize >= m_contentLen) {
        m_parseStartPos[m_contentLen] = END_CHAR ;
        printf("EVENT Request body:\n%s\n", m_parseStartPos);
        return PARSE_REQUEST_RETURN_CODE_FINISH;
    }

    return PARSE_REQUEST_RETURN_CODE_WAIT_FOR_READ;
}

bool HttpProcessor::Response(const ParseRequestReturnCode returnCode)
{
    switch (returnCode) {
        case PARSE_REQUEST_RETURN_CODE_FINISH: {
            ResponseStatusCode statusCode = HandleRequest();
            return FillResp(statusCode);
        }
        case PARSE_REQUEST_RETURN_CODE_ERROR: {
            return FillResp(RESPONSE_STATUS_CODE_BAD_REQUEST);
        }
        case PARSE_REQUEST_RETURN_CODE_CONTINUE: {
            return true;
        }
        default: {
            break;
        }
    }
    return false;
}

ResponseStatusCode HttpProcessor::HandleRequest()
{
    char filePath[MAX_FILE_NAME_LEN] = { 0 };
    int ret = sprintf(filePath, "%s%s", m_sourceDir.c_str(), m_url);
    if (ret == -1) {
       printf("ERROR Get file path fail, dir:%s, url:%s.\n", m_sourceDir.c_str(), m_url);
       return RESPONSE_STATUS_CODE_INTERNAL_SERVER_ERROR;
    }

    struct stat fileStat{ 0 };
    if (stat(filePath, &fileStat) == -1) {
       printf("ERROR Get file stat fail, path:%s.\n", filePath);
       return RESPONSE_STATUS_CODE_NOT_FOUND;    
    }

    if ((fileStat.st_mode & S_IROTH) == 0) {
        printf("ERROR Can't read %s.\n", filePath);
        return RESPONSE_STATUS_CODE_FORBIDDEN;
    }

    if (S_ISDIR(fileStat.st_mode)) {
        printf("ERROR %s is dir.\n", filePath);
        return RESPONSE_STATUS_CODE_BAD_REQUEST;
    }

    int fd = open(filePath, O_RDONLY);
    if (fd == -1) {
        printf("ERROR open file fail: %s.\n", filePath);
        return RESPONSE_STATUS_CODE_INTERNAL_SERVER_ERROR; 
    }
    m_fileSize = fileStat.st_size;
    m_fileAddr = reinterpret_cast<char *>(mmap(0, m_fileSize, PROT_READ, MAP_PRIVATE, fd, 0));
    close(fd);
    return RESPONSE_STATUS_CODE_OK;
}

bool HttpProcessor::FillResp(const ResponseStatusCode statusCode)
{
    if (statusCode == RESPONSE_STATUS_CODE_OK) {
        return FillRespInNormalCase();
    }
    for (unsigned int i = 0; i < ERROR_STATUS_INFO_LIST_SIZE; ++i) {
        if (statusCode == ERROR_STATUS_INFO_LIST[i].statusCode) {
            return FillRespInErrorCase(ERROR_STATUS_INFO_LIST[i]);
        }
    }

    printf("ERROR Invalid statusCode: %u.\n", statusCode);
    return false;
}

bool HttpProcessor::FillRespInNormalCase()
{
    if (!AddStatusLine(RESPONSE_STATUS_CODE_OK, OK_TITLE)) {
        return false;
    }
    if (!AddHeadField(m_fileSize)) {
        return false;
    }

    m_iov[STATUS_LINE_AND_HEAD_FIELD_VECTOR_INDEX].iov_base = m_writeBuff;
    m_iov[STATUS_LINE_AND_HEAD_FIELD_VECTOR_INDEX].iov_len = m_writeSize;
    m_iov[CONTENT_VECTOR_INDEX].iov_base = m_fileAddr;
    m_iov[CONTENT_VECTOR_INDEX].iov_len = m_fileSize;
    m_cnt = 2;
    m_leftRespSize = m_writeSize + m_fileSize;
    return true;
}

bool HttpProcessor::FillRespInErrorCase(const StatusInfo statusInfo)
{
    if (!AddStatusLine(statusInfo.statusCode, statusInfo.statusTitle)) {
        return false;
    }
    if (!AddHeadField(strlen(statusInfo.statusContent))) {
        return false;
    }
    if (!AddContent(statusInfo.statusContent)) {
        return false;
    }

    m_iov[STATUS_LINE_AND_HEAD_FIELD_VECTOR_INDEX].iov_base = m_writeBuff;
    m_iov[STATUS_LINE_AND_HEAD_FIELD_VECTOR_INDEX].iov_len = m_writeSize;
    m_cnt = 1;
    m_leftRespSize = m_writeSize;
    return true;
}

bool HttpProcessor::AddStatusLine(const int status, const char *title)
{
    int ret = sprintf(m_writeBuff, "%s %d %s\r\n",
        m_httpVersion, status, title);
    if (ret == -1) {
        printf("ERROR Write buffer fail.\n");
        return false;
    }
    m_writeSize += static_cast<unsigned int>(ret);

    return true;
}

bool HttpProcessor::AddHeadField(const unsigned int contentLen)
{
    if (m_writeSize >= MAX_WRITE_BUFF_LEN) {
        printf("ERROR Write buffer is full, m_writeSize = %u\n", m_writeSize);
        return false;
    }
    // 添加消息体长度
    int ret = sprintf(m_writeBuff + m_writeSize, "Content-Length: %d\r\n", contentLen);
    if (ret == -1) {
        printf("ERROR Write buffer fail.\n");
        return false;
    }
    m_writeSize += static_cast<unsigned int>(ret);
    // 添加连接方式
    ret = sprintf(m_writeBuff + m_writeSize, "Connection: %s\r\n", m_keepAlive ? KEEP_ALIVE_VALUE : CLOSE_ALIVE_VALUE);
    if (ret == -1) {
        printf("ERROR Write buffer fail.\n");
        return false;
    }
    m_writeSize += static_cast<unsigned int>(ret);
    // 添加空行
    ret = sprintf(m_writeBuff + m_writeSize, "\r\n");
    if (ret == -1) {
        printf("ERROR Write buffer fail.\n");
        return false;
    }
    m_writeSize += static_cast<unsigned int>(ret);
    return true;
}

bool HttpProcessor::AddContent(const char *content)
{
    if (m_writeSize >= MAX_WRITE_BUFF_LEN) {
        printf("ERROR Write buffer is full, m_writeSize = %u\n", m_writeSize);
        return false;
    }

    int ret = sprintf(m_writeBuff + m_writeSize, "%s", content);
    if (ret == -1) {
        printf("ERROR Write buffer fail.\n");
        return false;
    }
    m_writeSize += static_cast<unsigned int>(ret) + 1; // 1表示结束符
    return true;
}
