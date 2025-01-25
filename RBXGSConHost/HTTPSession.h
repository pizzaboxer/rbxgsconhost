#pragma once

typedef std::map<std::string, std::string> HTTPVariableTable;

class HTTPSession
{
private:
	bool parsedStatus, parsedHeaders;
	void ParseStatus(const std::string& line);
	void ParseHeader(const std::string& line);

public:
	bool finished, error;

	int messageLength, contentLength;
	char message[4096];
	const char *messagePos;

	std::string method, query, contentType;
	HTTPVariableTable serverVars, initServerVars;

	HTTPSession(const HTTPVariableTable& initServerVars);

	void Reset();
	void FeedBuffer(int receivedLength);
};