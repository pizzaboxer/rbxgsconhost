// LoaderTest.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include <httpext.h>
#include <crtdbg.h>
#include <windows.h>
#include <iphlpapi.h>
#include "stdint.h"
#include "HTTPConnection.h"

// specify in linker later
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Iphlpapi.lib")

// including this as part of the vcproj doesn't work properly,
// i'll try and fix this later
#include "picohttpparser.c"

typedef BOOL(WINAPI *GetExtensionVersion_t)(HSE_VERSION_INFO *);
typedef DWORD(WINAPI *HttpExtensionProc_t)(EXTENSION_CONTROL_BLOCK *);

char g_modulePath[256];
bool g_running = true;
char g_serverAddress[48];
char g_serverPort[6];
SOCKET g_serverSocket;

GetExtensionVersion_t g_getExtensionVersion;
HttpExtensionProc_t g_httpExtensionProc;

BOOL WINAPI GetServerVariable(HCONN hConn, LPSTR lpszVariableName, LPVOID lpvBuffer, LPDWORD lpdwSize)
{
	// printf("GetServerVariable called\n");
	// printf("%s %d\n", lpszVariableName, *lpdwSize);

	char *szBuffer = (char*)lpvBuffer;
	HTTPConnection *conn = HTTPConnection::Get((int)hConn);

	if (strcmp(lpszVariableName, "SERVER_NAME") == 0)
	{
		strcpy_s(szBuffer, *lpdwSize, conn->host);
		return TRUE;
	}

	if (strcmp(lpszVariableName, "SERVER_PROTOCOL") == 0)
	{
		sprintf_s(szBuffer, *lpdwSize, "HTTP/1.%d", conn->http_minor_ver);
		return TRUE;
	}

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

	if (strcmp(lpszVariableName, "URL") == 0)
	{
		strcpy_s(szBuffer, *lpdwSize, conn->path);
		return TRUE;
	}

	if (strlen(lpszVariableName) < 5)
	{
		SetLastError(ERROR_INVALID_INDEX);
		printf("Failed to GetServerVariable %s (invalid index)\n", lpszVariableName);
		return FALSE;
	}

	for (int i = 0; i != conn->num_headers; ++i)
	{
		if (_strnicmp(conn->headers[i].name, lpszVariableName + 5, conn->headers[i].name_len) == 0)
		{
			if (conn->headers[i].value_len > *lpdwSize)
			{
				SetLastError(ERROR_INSUFFICIENT_BUFFER);
				printf("Failed to GetServerVariable %s (insufficient buffer size)\n", lpszVariableName);
				return FALSE;
			}

			strncpy_s(szBuffer, *lpdwSize, conn->headers[i].value, conn->headers[i].value_len);
			return TRUE;
		}
	}

	SetLastError(ERROR_INVALID_INDEX);
	printf("Failed to GetServerVariable %s (index not found)\n", lpszVariableName);
	return FALSE;
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
		HSE_SEND_HEADER_EX_INFO *headerData = (HSE_SEND_HEADER_EX_INFO *)lpvBuffer;
		conn->response.append("HTTP/1.1 ");
		conn->response.append(headerData->pszStatus);
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

	char recvbuf[4096] = "";
	const char *method, *path_and_query;
	struct phr_header headers[100];
	size_t buflen = 0, prevbuflen = 0, method_len, path_and_query_len, num_headers;
	int pret, rret, minor_version;

	do
	{
		rret = recv(clientSocket, recvbuf + buflen, sizeof(recvbuf) - buflen, 0);

		prevbuflen = buflen;
		buflen += rret;

		if (rret > 0)
		{
			pret = phr_parse_request(recvbuf, buflen, &method, &method_len, &path_and_query, &path_and_query_len,
									 &minor_version, headers, &num_headers, prevbuflen);

			if (pret > 0)
			{
				break; /* successfully parsed the request */
			}
			else if (pret == -1)
			{
				conn->TerminateWithError(400);
				delete conn;
				return;
			}
			else if (buflen == sizeof(recvbuf))
			{
				conn->TerminateWithError(413);
				delete conn;
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
			conn->FlushAndClose();
			delete conn;
			return;
		}
	} while (rret > 0);

	conn->headers = (HTTPHeader *)headers;
	conn->num_headers = num_headers;
	conn->http_minor_ver = minor_version;
	conn->host = g_serverAddress;

	const char *contentType = "";
	int contentLength = 0;
	char path[256] = "", query[256] = "";

	for (int i = 0; i != num_headers; ++i)
	{
		if (_strnicmp(headers[i].name, "Content-Type", headers[i].name_len) == 0)
		{
			contentType = headers[i].value;

		}
		else if (_strnicmp(headers[i].name, "Content-Length", headers[i].name_len) == 0)
		{
			contentLength = atoi(headers[i].value);

		}
		else if (_strnicmp(headers[i].name, "Host", headers[i].name_len) == 0)
		{
			// exclude the port

			char host[64];
			const char *headerVal = headers[i].value;
			const char *portPos = strstr(headers[i].value, ":");
			int headerValLen = headers[i].value_len;

			if (portPos != NULL && portPos < headerVal + headerValLen)
				strncpy_s(host, headers[i].value, portPos - headerVal);
			else
				strncpy_s(host, headers[i].value, headers[i].value_len);

			conn->host = host;
		}
	}

	// picohttpparser does some weird things:
	// - keeps the query string as part of the path (for some reason)
	// - does not parse the request body
	// we need to do those ourselves

	const char *posQuery = strstr(path_and_query, "?");
	if (posQuery != NULL && posQuery < path_and_query + path_and_query_len)
	{
		const char *posEnd = strstr(posQuery, " ");

		if (posEnd != NULL)
		{
			posQuery++; // skip over question mark

			int query_len = posEnd - posQuery;
			int path_len = path_and_query_len - query_len - 1;

			strncpy_s(path, path_and_query, path_len);
			strncpy_s(query, posQuery, query_len);
		}
	}
	
	conn->path = path;

	EXTENSION_CONTROL_BLOCK ecb = {0};

	ecb.cbSize = sizeof(ecb);
	ecb.ConnID = (HCONN)conn->id;
	ecb.dwVersion = 393216; // IIS 6.0
	ecb.dwHttpStatusCode = 200;

	ecb.lpszPathInfo = (LPSTR)"/RBXGS/WebService.dll";
	ecb.lpszPathTranslated = g_modulePath;

	ecb.lpszMethod = (LPSTR)method;
	ecb.lpszQueryString = (LPSTR)query;
	ecb.cbTotalBytes = contentLength;
	ecb.cbAvailable = contentLength;
	ecb.lpbData = NULL;
	
	ecb.lpszContentType = (LPSTR)contentType;

	ecb.GetServerVariable = GetServerVariable;
	ecb.WriteClient = WriteClient;
	ecb.ReadClient = ReadClient;
	ecb.ServerSupportFunction = ServerSupportFunction;

	if (contentLength > 0)
	{
		char requestBody[4096];
		strncpy_s(requestBody, strstr(recvbuf, "\r\n\r\n") + 4, contentLength);
		ecb.lpbData = (LPBYTE)requestBody;
	}

	g_httpExtensionProc(&ecb);
}

bool InitializeWebService()
{
	HMODULE hModule = LoadLibraryEx(
		TEXT("WebService.dll"),
		NULL,
		LOAD_WITH_ALTERED_SEARCH_PATH);

	GetModuleFileNameA(hModule, g_modulePath, sizeof(g_modulePath));
	// printf("module path %s\n", g_modulePath);

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

int _tmain(int argc, _TCHAR *argv[])
{
	strcpy_s(g_serverPort, "64989");

	if (argc > 0)
	{
		for (int i = 0; i < argc-1; i++)
		{
			if (wcscmp(argv[i], L"-p") == 0 || wcscmp(argv[i], L"--port") == 0)
			{
				wchar_t *val = argv[i+1];
				int intVal = _wtoi(val);

				if (intVal > 0 && intVal < 65535)
					_itoa_s(intVal, g_serverPort, 10);
			}
		}
	}

	SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

	if (!InitializeWebService())
	{
		puts("Could not initialize web service\n");
		return 1;
	}

	HSE_VERSION_INFO versionInfo = {0};
	g_getExtensionVersion(&versionInfo);
	printf("Starting %s\n", versionInfo.lpszExtensionDesc);


	unsigned long lenAddresses = 16384;
	PIP_ADAPTER_ADDRESSES addresses = (PIP_ADAPTER_ADDRESSES)malloc(lenAddresses);

	if (addresses == NULL)
	{
		puts("Could not query adapters\n");
		return 1;
	}

	int result = GetAdaptersAddresses(AF_UNSPEC, NULL, NULL, addresses, &lenAddresses);
	
	if (result != ERROR_SUCCESS)
	{
		printf("Could not query adapters (%d)\n", result);
		return 1;
	}

	_IP_ADAPTER_UNICAST_ADDRESS *address = addresses->FirstUnicastAddress;
	DWORD serverAddressLen = sizeof(g_serverAddress);

	if (address == NULL)
	{
		puts("Could not query address\n");
		return 1;
	}

	WSAAddressToStringA(address->Address.lpSockaddr, address->Address.iSockaddrLength, NULL, g_serverAddress, &serverAddressLen);

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
