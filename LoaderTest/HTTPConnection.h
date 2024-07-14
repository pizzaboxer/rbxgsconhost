#pragma once

// same as phr_header from picohttpparser
struct HTTPHeader {
    const char *name;
    size_t name_len;
    const char *value;
    size_t value_len;
};

class HTTPConnection
{
private:
    SOCKET clientSocket;
    bool closed;

public:
    std::string response;
    
    HTTPHeader *headers;
    int num_headers, http_minor_ver;

    HTTPConnection(SOCKET clientSocket);
    ~HTTPConnection();

    static HTTPConnection* CreateNew(SOCKET clientSocket);
    static HTTPConnection* Get(SOCKET clientSocket);

    void TerminateWithError(int code);
    void FlushAndClose();
};