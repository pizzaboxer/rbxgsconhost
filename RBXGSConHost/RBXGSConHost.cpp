// LoaderTest.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include <httpext.h>
#include <crtdbg.h>
#include <windows.h>
#include <iphlpapi.h>

#include "HTTPConnection.h"

typedef BOOL(WINAPI *GetExtensionVersion_t)(HSE_VERSION_INFO *);
typedef DWORD(WINAPI *HttpExtensionProc_t)(EXTENSION_CONTROL_BLOCK *);

char g_modulePath[256];
bool g_running = true;
char g_serverAddress[48];
char g_serverPort[6] = "64989";
SOCKET g_serverSocket;

GetExtensionVersion_t g_getExtensionVersion;
HttpExtensionProc_t g_httpExtensionProc;

BOOL WINAPI GetServerVariable(HCONN hConn, LPSTR lpszVariableName, LPVOID lpvBuffer, LPDWORD lpdwSize)
{
	// printf("GetServerVariable called\n");
	// printf("%s %d\n", lpszVariableName, *lpdwSize);

	char *szBuffer = (char*)lpvBuffer;
	HTTPConnection *conn = HTTPConnection::Get((int)hConn);
	HTTPParser *parser = conn->parser;

	if (strcmp(lpszVariableName, "SERVER_PORT") == 0)
	{
		strcpy_s(szBuffer, *lpdwSize, g_serverPort);
		return TRUE;
	}

	if (strcmp(lpszVariableName, "HTTPS") == 0)
	{
		// should this follow X-Forwarded-Proto?
		strcpy_s(szBuffer, *lpdwSize, "off");
		return TRUE;
	}

	std::map<std::string, std::string>::iterator iter = parser->serverVars.find(lpszVariableName);

	if (iter == parser->serverVars.end())
	{
		SetLastError(ERROR_INVALID_INDEX);
		printf("Failed to GetServerVariable %s (index not found)\n", lpszVariableName);
		return FALSE;
	}

	if (iter->second.size() > *lpdwSize)
	{
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return FALSE;
	}

	strcpy_s(szBuffer, *lpdwSize, iter->second.c_str());
	return TRUE;
}

BOOL WINAPI WriteClient(HCONN ConnID, LPVOID Buffer, LPDWORD lpdwBytes, DWORD dwReserved)
{
	// printf("WriteClient called\n");

	char *szBuffer = (char*)Buffer;
	HTTPConnection *conn = HTTPConnection::Get((int)ConnID);

	std::stringstream response;

	if (strncmp(szBuffer, "<soap:", 6) == 0 || strncmp(szBuffer, "<?xml", 5) == 0)
		response << "Content-Type: text/xml\r\n";

	response << "Content-Length: " << *lpdwBytes << "\r\n";
	response << "Connection: close\r\n";
	response << "\r\n";
	response << szBuffer << "\r\n";

	conn->response.append(response.str());

	return TRUE;
}

BOOL WINAPI ReadClient(HCONN ConnID, LPVOID lpvBuffer, LPDWORD lpdwSize)
{
	printf("ReadClient called\n");
	return FALSE;
}

BOOL WINAPI ServerSupportFunction(HCONN hConn, DWORD dwHSERequest, LPVOID lpvBuffer, LPDWORD lpdwSize, LPDWORD lpdwDataType)
{
	// printf("ServerSupportFunction called\n");
	// printf("dwHSERequest: %d\n", dwHSERequest);
	// printf("lpdwSize: %x\n", *lpdwSize);

	HTTPConnection *conn = HTTPConnection::Get((int)hConn);

	switch (dwHSERequest)
	{
	case HSE_REQ_DONE_WITH_SESSION: // 4
		conn->FlushAndClose();
		delete conn;
		return TRUE;

	case HSE_REQ_GET_IMPERSONATION_TOKEN: // 1011
		HANDLE hToken;

		// TODO: is TOKEN_ALL_ACCESS appropriate?
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &hToken))
			return FALSE;

		if (!DuplicateToken(hToken, SecurityImpersonation, (PHANDLE)lpvBuffer))
			return FALSE;

		return TRUE;

	case HSE_REQ_SEND_RESPONSE_HEADER_EX: // 1016
		conn->response.append("HTTP/1.1 ");
		conn->response.append(((HSE_SEND_HEADER_EX_INFO *)lpvBuffer)->pszStatus);
		conn->response.append("\r\n");
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

void HandleHTTPRequest()
{
	SOCKET clientSocket = accept(g_serverSocket, NULL, NULL);

	if (clientSocket == INVALID_SOCKET)
	{
		printf("accept failed: %d\n", WSAGetLastError());
		return;
	}

	HTTPConnection *conn = HTTPConnection::CreateNew(clientSocket);
	HTTPParser *parser = conn->parser;
	parser->serverVars["SERVER_NAME"] = g_serverAddress;

	int rret;

	do
	{
		rret = recv(clientSocket, parser->message + parser->messageLength, sizeof(parser->message) - parser->messageLength, 0);

		if (rret > 0)
		{
			parser->FeedBuffer(rret);

			if (parser->finished)
			{
				break;
			}
			else if (parser->error)
			{
				conn->TerminateWithError(400);
				delete conn;
				return;
			}
			else if (parser->messageLength == sizeof(parser->message))
			{
				conn->TerminateWithError(413);
				delete conn;
				return;
			}
		}
		else if (rret == 0)
		{
			// printf("Connection closing...\n");
			conn->FlushAndClose();
			delete conn;
			return;
		}
		else
		{
			printf("Client socket receive error: %d\n", WSAGetLastError());
			conn->FlushAndClose();
			delete conn;
			return;
		}
	} while (rret > 0);

	EXTENSION_CONTROL_BLOCK ecb = {0};

	ecb.cbSize = sizeof(ecb);
	ecb.ConnID = (HCONN)conn->id;
	ecb.dwVersion = 393216; // IIS 6.0
	ecb.dwHttpStatusCode = 200;

	ecb.lpszPathInfo = (LPSTR)"/RBXGS/WebService.dll";
	ecb.lpszPathTranslated = g_modulePath;

	ecb.lpszMethod = (LPSTR)parser->method.c_str();
	ecb.lpszQueryString = (LPSTR)parser->query.c_str();
	ecb.cbTotalBytes = parser->contentLength;
	ecb.cbAvailable = parser->contentLength;
	ecb.lpbData = (LPBYTE)(parser->contentLength == 0 ? NULL : parser->body);
	
	ecb.lpszContentType = (LPSTR)parser->contentType.c_str();

	ecb.GetServerVariable = GetServerVariable;
	ecb.WriteClient = WriteClient;
	ecb.ReadClient = ReadClient;
	ecb.ServerSupportFunction = ServerSupportFunction;

	g_httpExtensionProc(&ecb);
}

bool InitializeWebService()
{
	HMODULE hModule = LoadLibraryEx(TEXT("WebService.dll"), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);

	if (hModule == NULL)
	{
		printf("Could not get Web service module handle: %d\n", GetLastError());
		return false;
	}

	GetModuleFileNameA(hModule, g_modulePath, sizeof(g_modulePath));

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

bool QueryServerAddress()
{
	unsigned long lenAddresses = 16384;
	PIP_ADAPTER_ADDRESSES addresses = (PIP_ADAPTER_ADDRESSES)malloc(lenAddresses);

	if (addresses == NULL)
	{
		puts("Could not query adapters\n");
		return false;
	}

	int result = GetAdaptersAddresses(AF_UNSPEC, NULL, NULL, addresses, &lenAddresses);
	
	if (result != ERROR_SUCCESS)
	{
		printf("Could not query adapters (%d)\n", result);
		return false;
	}

	_IP_ADAPTER_UNICAST_ADDRESS *address = addresses->FirstUnicastAddress;
	DWORD serverAddressLen = sizeof(g_serverAddress);

	if (address == NULL)
	{
		puts("Could not query address\n");
		return false;
	}

	WSAAddressToStringA(address->Address.lpSockaddr, address->Address.iSockaddrLength, NULL, g_serverAddress, &serverAddressLen);

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

int _tmain(int argc, _TCHAR *argv[])
{
	SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

	if (argc > 0)
	{
		for (int i = 0; i < argc-1; i++)
		{
			if (wcscmp(argv[i], L"-p") == 0 || wcscmp(argv[i], L"--port") == 0)
			{
				int port = _wtoi(argv[i+1]);

				if (port > 0 && port < 65535)
					_itoa_s(port, g_serverPort, 10);
			}
		}
	}

	if (!InitializeWebService())
	{
		puts("Could not initialize web service\n");
		return 1;
	}

	HSE_VERSION_INFO versionInfo = {0};
	g_getExtensionVersion(&versionInfo);
	printf("Starting %s\n", versionInfo.lpszExtensionDesc);

	if (!QueryServerAddress())
		return 1;

	if (!StartHTTPServer(g_serverPort))
	{
		puts("Could not start HTTP server\n");
		return 1;
	}

	printf("Listening on port %s\n", g_serverPort);

	while (g_running)
	{
		HandleHTTPRequest();
	}

	return 0;
}
