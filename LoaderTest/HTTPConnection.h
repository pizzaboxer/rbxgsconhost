#pragma once

class HTTPConnection
{
private:
    SOCKET clientSocket;
    bool closed;

public:
    std::string response;
    HTTPConnection(SOCKET clientSocket);
    ~HTTPConnection();
    void TerminateWithError(int code);
    static HTTPConnection* CreateNew(SOCKET clientSocket);
    static HTTPConnection* Get(SOCKET clientSocket);
    void FlushAndClose();
};