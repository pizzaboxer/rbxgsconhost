#include "stdafx.h"
#include "HTTPParser.h"

char transform_header(char c)
{
    if (c == '-')
        return '_';

    return toupper(c);
}

// yes this will be slow and heavy, but it doesn't matter

HTTPParser::HTTPParser()
{
    parsedStatus = false;
    parsedHeaders = false;
    parsedBody = false;

    finished = false;
    error = false;

    messageLength = 0;
    contentLength = 0;

    serverVars["URL"] = "/";

    messagePos = message;
}

void HTTPParser::FeedBuffer(int receivedLength)
{
    if (finished || error)
        return;

    messageLength += receivedLength;

    if (!parsedStatus || !parsedHeaders)
    {
        const char *endpos = strstr(messagePos, "\r\n");

        while (endpos != NULL)
        {
            int length = endpos - messagePos;
            std::string line(messagePos, length);

            if (!parsedStatus)
                ParseStatus(line);
            else if (!parsedHeaders)
                ParseHeader(line);

            if (error)
                return;

            messagePos += length + 2;

            endpos = strstr(messagePos, "\r\n");
        }
    }

    if (parsedStatus && parsedHeaders)
    {
        if (contentLength == 0)
        {
            finished = true;
            return;
        }

        if (messageLength >= (body - message) + contentLength)
            finished = true;
    }
}

void HTTPParser::ParseStatus(std::string& line)
{
    // split lines
    std::string s;
    std::stringstream ss(line);
    std::vector<std::string> parts;

    while (std::getline(ss, s, ' '))
        parts.push_back(s);

    if (parts.size() != 3)
    {
        puts("HTTP PARSE ERROR: could not validate status line");
        error = true;
        return;
    }

    // validate method
    for (std::string::iterator it = parts[0].begin(); it != parts[0].end(); ++it)
    {
        if (!isalpha(*it) || !isupper(*it))
        {
            puts("HTTP PARSE ERROR: could not validate method");
            error = true;
            return;
        }
    }

    if (parts[2] != "HTTP/1.0" && parts[2] != "HTTP/1.1")
    {
        puts("HTTP PARSE ERROR: could not validate protocol");
        error = true;
        return;
    }

    std::size_t queryPos = parts[1].find("?");

    serverVars["URL"] = parts[1].substr(0, queryPos);

    if (queryPos != std::string::npos)
        query = parts[1].substr(queryPos + 1, parts[1].find(" "));

    method = parts[0];
    serverVars["SERVER_PROTOCOL"] = parts[2];

    parsedStatus = true;
}

void HTTPParser::ParseHeader(std::string& line)
{
    std::size_t separator = line.find(": ");

    if (separator == std::string::npos)
    {
        if (line.empty())
        {
            parsedHeaders = true;
            body = messagePos + 2; // messagePos is currently positioned at \r\n{body}
        }
        else
        {
            puts("HTTP PARSE ERROR: could not validate header");
            error = true;
        }

        return;
    }

    std::string name = line.substr(0, separator);
    std::string value = line.substr(separator+2);

    for (std::string::iterator it = name.begin(); it != name.end(); ++it)
    {
        if (!isalpha(*it) && *it != '-')
        {
            puts("HTTP PARSE ERROR: could not validate header name");
            error = true;
            return;
        }
    }

    // convert to isapi format, e.g. User-Agent becomes HTTP_USER_AGENT
    std::transform(name.begin(), name.end(), name.begin(), transform_header);
    name.insert(0, "HTTP_");
    
    serverVars[name] = value;

    if (name == "HTTP_CONTENT_TYPE")
        contentType = value;
        
    else if (name == "HTTP_CONTENT_LENGTH")
        contentLength = atoi(value.c_str());

    else if (name == "HTTP_HOST")
        serverVars["SERVER_NAME"] = value.substr(0, value.find(":"));
}
