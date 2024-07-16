#pragma once

class HTTPParser
{
private:
    bool parsedStatus, parsedHeaders, parsedBody;
    void ParseStatus(std::string& line);
    void ParseHeader(std::string& line);

public:
    bool finished, error;
    int messageLength, contentLength;
    char message[4096];
    std::string method, query, contentType;
    std::map<std::string, std::string> serverVars;
    const char *body, *messagePos;

    HTTPParser();
    void FeedBuffer(int receivedLength);
};