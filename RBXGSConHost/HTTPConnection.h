#pragma once

#include "HTTPParser.h"

class HTTPConnection
{
private:
    SOCKET clientSocket;
    bool closed;

public:
    int id;

    HTTPParser *parser;
    std::string response;

    HTTPConnection(int id, SOCKET clientSocket);
    ~HTTPConnection();

    static HTTPConnection *CreateNew(SOCKET clientSocket);
    static HTTPConnection *Get(int id);

    void TerminateWithError(int code);
    void FlushAndClose();
};