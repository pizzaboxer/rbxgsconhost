#include "stdafx.h"
#include "HTTPConnection.h"

std::map<SOCKET, HTTPConnection*> g_httpConnections;

HTTPConnection::HTTPConnection(SOCKET clientSocket)
{
    this->clientSocket = clientSocket;
    this->response = "";
    this->closed = false;
}

HTTPConnection::~HTTPConnection()
{
    g_httpConnections.erase(clientSocket);
}

HTTPConnection* HTTPConnection::CreateNew(SOCKET clientSocket)
{
    HTTPConnection *conn = new HTTPConnection(clientSocket);
    g_httpConnections.insert(std::pair<SOCKET, HTTPConnection*>(clientSocket, conn));
    return conn;
}

HTTPConnection* HTTPConnection::Get(SOCKET clientSocket)
{
    std::map<SOCKET, HTTPConnection*>::iterator iter = g_httpConnections.find(clientSocket);

	if (iter == g_httpConnections.end())
	{
		printf("Invalid socket %d\n", clientSocket);
		return NULL;
	}

    return iter->second;
}

void HTTPConnection::TerminateWithError(int code)
{
	char *statusLine = "";
	char body[64];
	std::stringstream ssResponse;

	switch (code)
	{
	case 400:
		statusLine = "Bad Request";
		break;
	case 413:
		statusLine = "Content Too Large";
		break;
	}

	ssResponse << "HTTP/1.1 " << code;

	if (statusLine == "")
	{
		ssResponse << "\r\n";
		sprintf_s(body, "<h1>HTTP %d</h1>", code);
	}
	else
	{
		ssResponse << " " << statusLine << "\r\n";
		sprintf_s(body, "<h1>%s</h1>", statusLine);
	}

	ssResponse << "Content-Type: text/html\r\n";
	ssResponse << "Connection: close\r\n";
	ssResponse << "Content-Length: " << strlen(body) << "\r\n";
	ssResponse << "\r\n";
	ssResponse << body;

	response = ssResponse.str();

	FlushAndClose();
}

void HTTPConnection::FlushAndClose()
{
    if (send(clientSocket, response.c_str(), response.length(), 0) == SOCKET_ERROR)
		printf("Warning: client socket send error! (%d)\n", WSAGetLastError());

	closesocket(clientSocket);
}