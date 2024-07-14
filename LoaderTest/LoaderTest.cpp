// LoaderTest.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include <httpext.h>
#include <crtdbg.h>
#include <sstream>
#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include "stdint.h"

// specify in linker later
#pragma comment(lib, "Ws2_32.lib")

// including this as part of the vcproj doesn't work properly,
// i'll try and fix this later
#include "picohttpparser.c"

typedef BOOL(WINAPI* GetExtensionVersion_t)(HSE_VERSION_INFO*);
typedef DWORD(WINAPI* HttpExtensionProc_t)(EXTENSION_CONTROL_BLOCK*);

bool g_running = true;
SOCKET g_serverSocket;

std::map<SOCKET, std::string*> g_clientConnections;

GetExtensionVersion_t g_getExtensionVersion;
HttpExtensionProc_t g_httpExtensionProc;

void FlushAndTerminateHTTPClient(SOCKET clientSocket)
{
	std::map<SOCKET, std::string*>::iterator conn = g_clientConnections.find(clientSocket);

	if (conn == g_clientConnections.end())
	{
		printf("Warning: tried to flush invalid socket %d\n", clientSocket);
		return;
	}

	if (send(clientSocket, conn->second->c_str(), conn->second->length(), 0) == SOCKET_ERROR)
		printf("Warning: client socket send error (%d)\n", WSAGetLastError());

	closesocket(clientSocket);

	g_clientConnections.erase(clientSocket);
}

BOOL WINAPI GetServerVariable(HCONN hConn, LPSTR lpszVariableName, LPVOID lpvBuffer, LPDWORD lpdwSize)
{
    printf("GetServerVariable called\n");
	printf("%s\n", lpszVariableName);
    return FALSE;
}

BOOL WINAPI WriteClient(HCONN ConnID, LPVOID Buffer, LPDWORD lpdwBytes, DWORD dwReserved)
{
    printf("WriteClient called\n");

	SOCKET clientSocket = (SOCKET)ConnID;

	std::map<SOCKET, std::string*>::iterator conn = g_clientConnections.find(clientSocket);

	if (conn == g_clientConnections.end())
	{
		printf("Invalid socket %d\n", clientSocket);
		return FALSE;
	}

	std::stringstream response;

	response << "Content-Type: text/html\r\n";
	response << "Content-Length: " << *lpdwBytes << "\r\n";
	response << "\r\n";
	response << (char*)Buffer << "\r\n";

	conn->second->append(response.str());
	
    return TRUE;
}

BOOL WINAPI ReadClient(HCONN ConnID, LPVOID lpvBuffer, LPDWORD lpdwSize)
{
    printf("ReadClient called\n");
    return FALSE;
}

BOOL WINAPI ServerSupportFunction(HCONN hConn, DWORD dwHSERequest, LPVOID lpvBuffer, LPDWORD lpdwSize, LPDWORD lpdwDataType)
{
    printf("ServerSupportFunction called\n");
    printf("dwHSERequest: %d\n", dwHSERequest);
    // printf("lpdwSize: %x\n", *lpdwSize);

	SOCKET clientSocket = (SOCKET)hConn;

	std::map<SOCKET, std::string*>::iterator conn = g_clientConnections.find(clientSocket);

	if (conn == g_clientConnections.end())
	{
		printf("Invalid socket %d\n", clientSocket);
		return FALSE;
	}

    switch (dwHSERequest)
    {
	case HSE_REQ_DONE_WITH_SESSION: //4
		FlushAndTerminateHTTPClient(clientSocket);
		return TRUE;

    case HSE_REQ_GET_IMPERSONATION_TOKEN: //1011
		HANDLE hToken;

		// TODO: is TOKEN_ALL_ACCESS appropriate?
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &hToken))
			return FALSE;

		if (!DuplicateToken(hToken, SecurityImpersonation, (PHANDLE)lpvBuffer))
			return FALSE;

		return TRUE;

    case HSE_REQ_SEND_RESPONSE_HEADER_EX: //1016
		HSE_SEND_HEADER_EX_INFO *headerData = (HSE_SEND_HEADER_EX_INFO*)lpvBuffer;
		conn->second->append("HTTP/1.1 ");
		conn->second->append(headerData->pszStatus);
		conn->second->append("\r\n");
        return TRUE;
    }

	printf("dwHSERequest: %d not handled!\n", dwHSERequest);

    return FALSE;
}

bool StartHTTPServer(const char *port)
{
    int result;

    struct addrinfo addrHints = {0}, *addrResult = NULL;
    addrHints.ai_family = AF_INET;
    addrHints.ai_socktype = SOCK_STREAM;
    addrHints.ai_protocol = IPPROTO_TCP;
    addrHints.ai_flags = AI_PASSIVE;

    result = getaddrinfo(NULL, port, &addrHints, &addrResult);
    if (result != 0)
    {
        printf("getaddrinfo failed: %d", result);
        return false;
    }

    g_serverSocket = socket(addrResult->ai_family, addrResult->ai_socktype, addrResult->ai_protocol);
    if (g_serverSocket == INVALID_SOCKET)
    {
        printf("Unable to create socket: %ld\n", WSAGetLastError());
        freeaddrinfo(addrResult);
        WSACleanup();
        return false;
    }

    if (bind(g_serverSocket, addrResult->ai_addr, (int)addrResult->ai_addrlen) == SOCKET_ERROR)
    {
        printf("Unable to bind socket on port %s: %ld\n", port, WSAGetLastError());
        freeaddrinfo(addrResult);
        closesocket(g_serverSocket);
        WSACleanup();
        return false;
    }

    if (listen(g_serverSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        printf("Unable to listen on socket: %ld\n", WSAGetLastError());
        closesocket(g_serverSocket);
        WSACleanup();
        return false;
    }

    return true;
}

void TerminateHTTPClientStatus(SOCKET clientSocket, int code)
{
	std::map<SOCKET, std::string*>::iterator conn = g_clientConnections.find(clientSocket);

	if (conn == g_clientConnections.end())
	{
		printf("Invalid socket %d\n", clientSocket);
		return;
	}

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

	conn->second->assign(ssResponse.str());

	FlushAndTerminateHTTPClient(clientSocket);
}

void HandleHTTPRequest()
{
    SOCKET clientSocket = accept(g_serverSocket, NULL, NULL);

	if (clientSocket == INVALID_SOCKET)
    {
	    printf("accept failed: %d\n", WSAGetLastError());
        return;
    }

	g_clientConnections.insert(std::pair<SOCKET, std::string*>(clientSocket, new std::string("")));

    char recvbuf[4096];
	const char *method, *path;
	struct phr_header headers[100];
	size_t buflen = 0, prevbuflen = 0, method_len, path_len, num_headers;
	int pret, rret, minor_version;

	do 
	{
		rret = recv(clientSocket, recvbuf + buflen, sizeof(recvbuf) - buflen, 0);

		prevbuflen = buflen;
		buflen += rret;

		if (rret > 0) 
		{
			pret = phr_parse_request(recvbuf, buflen, &method, &method_len, &path, &path_len,
				&minor_version, headers, &num_headers, prevbuflen);

			if (pret > 0)
			{
        		break; /* successfully parsed the request */
			}
    		else if (pret == -1)
			{
				TerminateHTTPClientStatus(clientSocket, 400);
				return;
			}
			else if (buflen == sizeof(recvbuf))
			{
				TerminateHTTPClientStatus(clientSocket, 413);
				return;
			}
			
			_ASSERT(pret == -2);
		} 
		else if (rret == 0)
		{
			printf("Connection closing...\n");
			return;
		} 
		else 
		{
			printf("Client socket receive error: %d\n", WSAGetLastError());
			closesocket(clientSocket);
			g_clientConnections.erase(clientSocket);
			return;
		}
	} while (rret > 0);

	printf("request is %d bytes long\n", pret);
	printf("method is %.*s\n", (int)method_len, method);
	printf("path is %.*s\n", (int)path_len, path);
	printf("HTTP version is 1.%d\n", minor_version);
	printf("headers:\n");

	char queryString[256];
	const char *contentType = "";
	int contentLength = 0;
	char requestBody[4096];

	for (int i = 0; i != num_headers; ++i) {
		
		if (strnicmp(headers[i].name, "Content-Type", headers[i].name_len) == 0)
			contentType = headers[i].value;
		
		if (strnicmp(headers[i].name, "Content-Length", headers[i].name_len) == 0)
			contentLength = atoi(headers[i].value);
	}

	// parse query string
	const char *pos = strstr(path, "?");
	if (pos != NULL)
	{
		pos++; //skip over question mark
		strncpy_s(queryString, pos, strstr(pos, " ")-pos);
	}

	if (contentLength > 0)
		strncpy_s(requestBody, strstr(recvbuf, "\r\n\r\n") + 4, contentLength);

    EXTENSION_CONTROL_BLOCK ecb = {0};

    ecb.cbSize = sizeof(ecb);
    ecb.ConnID = (HCONN)clientSocket;
    ecb.dwVersion = 393216; // IIS 6.0
    ecb.dwHttpStatusCode = 200;
    ecb.lpszMethod = (LPSTR)method;
    ecb.lpszQueryString = (LPSTR)queryString;
    ecb.lpszPathInfo = (LPSTR) "/RBXGS/WebService.dll";
    ecb.lpszPathTranslated = (LPSTR) "C:\\inetpub\\wwwroot\\RBXGS\\WebService.dll";
	ecb.cbTotalBytes = contentLength;
	ecb.cbAvailable = contentLength;
	ecb.lpbData = (LPBYTE)(contentLength > 0 ? requestBody : 0);
    ecb.lpszContentType = (LPSTR)contentType;
    ecb.GetServerVariable = GetServerVariable;
    ecb.WriteClient = WriteClient;
    ecb.ReadClient = ReadClient;
    ecb.ServerSupportFunction = ServerSupportFunction;

    g_httpExtensionProc(&ecb);
}

bool InitializeWebService()
{
    HMODULE hModule = LoadLibraryEx(
		TEXT("C:\\inetpub\\wwwroot\\RBXGS\\WebService.dll"), 
		NULL, 
		LOAD_WITH_ALTERED_SEARCH_PATH
	);

	if (hModule == NULL)
	{
		printf("Could not get Web service module handle: %d\n", GetLastError());
		return false;
	}

	// calling GetExtensionVersion is necessary for initialization
	g_getExtensionVersion = (GetExtensionVersion_t)GetProcAddress(hModule, "GetExtensionVersion");
	g_httpExtensionProc = (HttpExtensionProc_t)GetProcAddress(hModule, "HttpExtensionProc");

	if (g_getExtensionVersion == NULL)
	{
		printf("Could not get proc address for GetExtensionVersion: %d\n", GetLastError());
		return false;
	}

	if (g_httpExtensionProc == NULL)
	{
		printf("Could not get proc address for HttpExtensionProc: %d\n", GetLastError());
		return false;
	}

    return true;
}

BOOL WINAPI ConsoleCtrlHandler(DWORD fdwCtrlType)
{
	switch (fdwCtrlType)
	{
	case CTRL_CLOSE_EVENT:
		printf("CTRL_CLOSE_EVENT\n");
		g_running = false;
		return TRUE;
	}

	return FALSE;
}

int _tmain(int argc, _TCHAR* argv[])
{
	const char *port = "64989";

	SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
	
	if (!InitializeWebService())
	{
		puts("Could not initialize web service\n");
		return 1;
	}

	HSE_VERSION_INFO versionInfo = {0};
	g_getExtensionVersion(&versionInfo);
	printf("Starting %s\n", versionInfo.lpszExtensionDesc);

	if (!StartHTTPServer(port))
	{
		puts("Could not start HTTP server\n");
		return 1;
	}

	printf("Listening on port %s\n", port);

	while (g_running)
	{
		HandleHTTPRequest();
	}



	// 	if (NULL != httpExtensionProc)
	// 	{
	// 		/*printf("GetServerVariable: %p\n", GetServerVariable);
	// 		printf("ReadClient: %p\n", ReadClient);
	// 		printf("WriteClient: %p\n", WriteClient);
	// 		printf("ServerSupportFunction: %p\n", ServerSupportFunction);*/
	     
	// 		EXTENSION_CONTROL_BLOCK ecb = { 0 };

	// 		/*printf("ecb: %p\n", &ecb);
	// 		printf("ecb.WriteClient: %p\n", &ecb.WriteClient);
	// 		printf("ecb.ServerSupportFunction: %p\n", &ecb.ServerSupportFunction);*/

	// 		ecb.cbSize = sizeof(ecb);
	// 		ecb.ConnID = (HCONN)34632;
	// 		ecb.dwVersion = 393216; // IIS 6.0
	// 		ecb.dwHttpStatusCode = 200;
	// 		ecb.lpszMethod = (LPSTR)"GET";
	// 		ecb.lpszQueryString = (LPSTR)"";
	// 		ecb.lpszPathInfo = (LPSTR)"/RBXGS/WebService.dll";
	// 		ecb.lpszPathTranslated = (LPSTR)"C:\\inetpub\\wwwroot\\RBXGS\\WebService.dll";
	// 		ecb.lpszContentType = (LPSTR)"";
	// 		ecb.GetServerVariable = GetServerVariable;
	// 		ecb.WriteClient = WriteClient;
	// 		ecb.ReadClient = ReadClient;
	// 		ecb.ServerSupportFunction = ServerSupportFunction;

	// 		httpExtensionProc(&ecb);
	// 	}
	// 	else
	// 	{
	// 		printf("Could not get proc address: %d\n", GetLastError());
	// 	}
	// }
	// else
	// {
	// 	printf("Could not get module handle: %d\n", GetLastError());
	// }

	return 0;
}

