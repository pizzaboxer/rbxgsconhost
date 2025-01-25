#include "stdafx.h"
#include "HTTPSession.h"

#include <algorithm>

char transform_header(char c)
{
	if (c == '-')
		return '_';

	return toupper(c);
}

HTTPSession::HTTPSession(const HTTPVariableTable& initServerVars) 
	: initServerVars(initServerVars)
{
	Reset();
}

void HTTPSession::Reset()
{	
	parsedStatus = false;
	parsedHeaders = false;

	finished = false;
	error = false;

	messageLength = 0;
	contentLength = 0;
	
	messagePos = message;

	method.clear();
	query.clear();
	contentType.clear();

	serverVars.swap(initServerVars);
}

void HTTPSession::FeedBuffer(int receivedLength)
{
	if (finished || error)
		return;

	messageLength += receivedLength;

	if (!parsedStatus || !parsedHeaders)
	{
		const char *endpos = strstr(messagePos, "\r\n");

		while (endpos != NULL && (!parsedStatus || !parsedHeaders))
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
		if (messageLength >= (messagePos - message) + contentLength)
			finished = true;
	}
}

void HTTPSession::ParseStatus(const std::string& line)
{
	// split by spaces
	std::string s;
	std::stringstream ss(line);
	std::vector<std::string> parts;

	while (std::getline(ss, s, ' '))
		parts.push_back(s);

	if (parts.size() != 3)
	{
#ifdef _DEBUG
		puts("HTTP PARSE ERROR: could not validate status line");
#endif
		error = true;
		return;
	}

	// validate method
	for (std::string::iterator it = parts[0].begin(); it != parts[0].end(); ++it)
	{
		if (!isalpha(*it) || !isupper(*it))
		{
#ifdef _DEBUG
			puts("HTTP PARSE ERROR: could not validate method");
#endif
			error = true;
			return;
		}
	}

	if (parts[2] != "HTTP/1.0" && parts[2] != "HTTP/1.1")
	{
#ifdef _DEBUG
		puts("HTTP PARSE ERROR: could not validate protocol");
#endif
		error = true;
		return;
	}

	std::size_t queryPos = parts[1].find("?");

	serverVars["URL"] = parts[1].substr(0, queryPos);

	method = parts[0];

	if (queryPos != std::string::npos)
		query = parts[1].substr(queryPos + 1, parts[1].find(" "));

	serverVars["SERVER_PROTOCOL"] = parts[2];

	parsedStatus = true;
}

void HTTPSession::ParseHeader(const std::string& line)
{
	std::size_t separator = line.find(":");

	if (separator == std::string::npos)
	{
		if (line.empty())
		{
			parsedHeaders = true;
		}
		else
		{
#ifdef _DEBUG
			puts("HTTP PARSE ERROR: could not validate header");
#endif
			error = true;
		}

		return;
	}

	std::string name = line.substr(0, separator);
	std::string value = line.substr(separator+1);

	// remove whitespace
	// https://stackoverflow.com/questions/83439/remove-spaces-from-stdstring-in-c
	value.erase(remove_if(value.begin(), value.end(), isspace), value.end());

	for (std::string::iterator it = name.begin(); it != name.end(); ++it)
	{
		if (!isalpha(*it) && *it != '-')
		{
#ifdef _DEBUG
			puts("HTTP PARSE ERROR: could not validate header");
#endif
			error = true;
			return;
		}
	}

	// convert to isapi variable format, e.g. User-Agent becomes HTTP_USER_AGENT
	std::transform(name.begin(), name.end(), name.begin(), transform_header);
	name = "HTTP_" + name;
	
	serverVars[name] = value;

	if (!value.empty())
	{
		if (name == "HTTP_CONTENT_TYPE")
			contentType = value;
		else if (name == "HTTP_CONTENT_LENGTH")
			contentLength = atoi(value.c_str());
		else if (name == "HTTP_HOST")
		{
			std::string hostname = value.substr(0, value.find_last_of(":"));

			for (std::string::iterator it = hostname.begin(); it != hostname.end(); ++it)
			{
				if (!isalpha(*it) && !isdigit(*it) && strchr("@-.", *it) == NULL)
				{
	#ifdef _DEBUG
					puts("HTTP PARSE ERROR: could not validate hostname");
	#endif
					error = true;
					return;
				}
			}
			
			serverVars["SERVER_NAME"] = hostname;
		}
	}
}
