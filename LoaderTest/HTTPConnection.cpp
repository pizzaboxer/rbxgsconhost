#include "stdafx.h"
#include "HTTPConnection.h"

std::map<SOCKET, HTTPConnection*> g_httpConnections;

HTTPConnection::HTTPConnection(SOCKET socket)
{
    this->socket = socket;
}