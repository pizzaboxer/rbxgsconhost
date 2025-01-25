#include "stdafx.h"
#include "HTTPConnection.h"

HTTPConnection::HTTPConnection(SOCKET clientSocket)
	: clientSocket(clientSocket),
	sessionPresent(false),
	keepAlive(true)
{
}

HTTPConnection::~HTTPConnection()
{
#ifdef _DEBUG
	puts("HTTPConnection destroyed");
#endif
}

HTTPConnection* HTTPConnection::Get(HCONN hConn)
{
	return static_cast<HTTPConnection*>(hConn);
}

void HTTPConnection::WriteHeaders(const char *status, const char *additional)
{
	std::ostringstream httpOut;

	httpOut << "HTTP/1.1 " << status << "\r\n";
	httpOut << "Server: RBXGSConHost\r\n";

	time_t rawtime;
	struct tm timeinfo;
	char timeHeaderBuf[64];

	time(&rawtime);
	if (_gmtime64_s(&timeinfo, &rawtime) == 0)
	{
		strftime(timeHeaderBuf, sizeof(timeHeaderBuf), "%a, %d %b %Y %H:%M:%S GMT", &timeinfo);
		httpOut << "Date: " << timeHeaderBuf << "\r\n";
	}

	if (!keepAlive)
		httpOut << "Connection: close\r\n";

	if (additional == NULL)
		httpOut << "\r\n";
	else
		httpOut << additional;

	Write(httpOut.str());
}

void HTTPConnection::Write(const char *buf, int len)
{
	if (send(clientSocket, buf, len, 0) == SOCKET_ERROR)
		printf("WARNING: client socket send error (%d)\n", WSAGetLastError());
}

void HTTPConnection::Write(std::string content)
{
	Write(content.c_str(), (int)content.length());
}

void HTTPConnection::StartSession(const HTTPSession *session)
{
#ifdef _DEBUG
	puts("Session start");
#endif

	this->session = session;
	sessionPresent = true;
}

void HTTPConnection::EndSession()
{
#ifdef _DEBUG
	puts("Session end");
#endif

	if (sessionPresent)
	{
		sessionPresent = false;
		delete session;
	}

	if (!keepAlive)
	{
#ifdef _DEBUG
		puts("Closing connection");
#endif
		shutdown(clientSocket, SD_SEND);
	}
}

const HTTPSession* HTTPConnection::GetSession() const
{
	if (!sessionPresent)
		return NULL;

	return session;
}

void HTTPConnection::SendError(int code)
{
#ifdef _DEBUG
	printf("Sending HTTP %d\n", code);
#endif

	std::ostringstream httpOut;
	std::string status;

	switch (code)
	{
	case 400:
		status = "400 Bad Request";
		break;
	case 413:
		status = "413 Content Too Large";
		break;
	case 500:
		status = "500 Internal Server Error";
		break;
	default:
		return;
	}

	std::string body("<h1>" + status + "</h1>");

	httpOut << "Content-Type: text/html\r\n";
	httpOut << "Content-Length: " << body.length() << "\r\n";
	httpOut << "\r\n";
	httpOut << body;

	keepAlive = false;

	WriteHeaders(status.c_str(), httpOut.str().c_str());
	EndSession();
}

void HTTPConnection::Close()
{
	closesocket(clientSocket);
}