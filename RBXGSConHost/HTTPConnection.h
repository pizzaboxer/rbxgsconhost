#pragma once

#include "HTTPSession.h"

class HTTPConnection
{
private:
	const SOCKET clientSocket;
	const HTTPSession *session;
	bool sessionPresent;

public:
	bool keepAlive;

	HTTPConnection(SOCKET clientSocket);
	~HTTPConnection();

	static HTTPConnection* Get(HCONN hConn);

	void WriteHeaders(const char *status, const char *additional);
	void Write(const char *buf, int len);
	void Write(std::string content);

	void StartSession(const HTTPSession *session);
	void EndSession();
	const HTTPSession* GetSession() const;

	void SendError(int code);
	void Close();
};