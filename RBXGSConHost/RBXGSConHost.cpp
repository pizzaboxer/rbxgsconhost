// LoaderTest.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include <tchar.h>
#include <iphlpapi.h>
#include <shlwapi.h>

#include "HTTPConnection.h"
#include "WebService.h"
#include "Hooks.h"

bool g_running = false;
char g_modulePath[MAX_PATH];
char g_contentFolderPath[MAX_PATH];
SOCKET g_serverSocket;
HTTPVariableTable g_initVars;
std::set<HTTPConnection*> g_serverConnections;

BOOL WINAPI ISAPIReadClient(HCONN ConnID, LPVOID lpvBuffer, LPDWORD lpdwSize)
{
	printf("ReadClient called\n");
	return FALSE;
}

BOOL WINAPI ISAPIWriteClient(HCONN ConnID, LPVOID Buffer, LPDWORD lpdwBytes, DWORD dwReserved)
{
	HTTPConnection::Get(ConnID)->Write(static_cast<const char*>(Buffer), *lpdwBytes);
	return TRUE;
}

BOOL WINAPI ISAPIGetServerVariable(HCONN hConn, LPSTR lpszVariableName, LPVOID lpvBuffer, LPDWORD lpdwSize)
{
	HTTPConnection *conn = HTTPConnection::Get(hConn);
	const HTTPSession *session = conn->GetSession();
		
	HTTPVariableTable::const_iterator it = session->serverVars.find(lpszVariableName);

	if (it == session->serverVars.end())
	{
		SetLastError(ERROR_INVALID_INDEX);
#ifdef _DEBUG
		printf("Failed to GetServerVariable %s (index not found)\n", lpszVariableName);
#endif
		return FALSE;
	}

	const std::string &value = it->second;

	if (value.size() > *lpdwSize)
	{
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return FALSE;
	}

	strcpy_s(static_cast<char*>(lpvBuffer), *lpdwSize, value.c_str());
	return TRUE;
}

BOOL WINAPI ISAPIServerSupportFunction(HCONN hConn, DWORD dwHSERequest, LPVOID lpvBuffer, LPDWORD lpdwSize, LPDWORD lpdwDataType)
{
	HTTPConnection *conn = HTTPConnection::Get(hConn);

	switch (dwHSERequest)
	{
	case HSE_REQ_DONE_WITH_SESSION: // 4
		conn->EndSession();
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
		LPHSE_SEND_HEADER_EX_INFO headerInfo = static_cast<LPHSE_SEND_HEADER_EX_INFO>(lpvBuffer);

		conn->keepAlive = headerInfo->fKeepConn == TRUE;
		conn->WriteHeaders(headerInfo->pszStatus, headerInfo->pszHeader); 

		return TRUE;
	}

	printf("dwHSERequest: %d not handled!\n", dwHSERequest);

	return FALSE;
}

bool QueryInterfaceAddress(LPSTR szAddress, DWORD lenAddress)
{
	unsigned long lenAddresses = 16384;
	PIP_ADAPTER_ADDRESSES addresses = (PIP_ADAPTER_ADDRESSES)malloc(lenAddresses);

	if (addresses == NULL)
	{
		puts("Could not query adapters");
		return false;
	}

	int result = GetAdaptersAddresses(AF_UNSPEC, NULL, NULL, addresses, &lenAddresses);
	
	if (result != ERROR_SUCCESS)
	{
		printf("Could not query adapters (%d)\n", result);
		return false;
	}

	_IP_ADAPTER_UNICAST_ADDRESS *address = addresses->FirstUnicastAddress;

	if (address == NULL)
	{
		puts("Could not query address");
		return false;
	}

	WSAAddressToStringA(address->Address.lpSockaddr, 
		address->Address.iSockaddrLength, NULL, szAddress, &lenAddress);

	return true;
}

bool StartHTTPServer(const char *port)
{
	// winsock is implicitly initialized by RBXGS, seems to be <2.2.0

	int result;

	struct addrinfo addrHints = {0}, *addrResult = NULL;
	addrHints.ai_family = AF_INET;
	addrHints.ai_socktype = SOCK_STREAM;
	addrHints.ai_protocol = IPPROTO_TCP;
	addrHints.ai_flags = AI_PASSIVE;

	result = getaddrinfo(NULL, port, &addrHints, &addrResult);
	if (result != 0)
	{
		printf("getaddrinfo failed: %d\n", result);
		return false;
	}

	g_serverSocket = socket(addrResult->ai_family, addrResult->ai_socktype, addrResult->ai_protocol);
	if (g_serverSocket == INVALID_SOCKET)
	{
		printf("Unable to create socket: %ld\n", WSAGetLastError());
		return false;
	}

	if (bind(g_serverSocket, addrResult->ai_addr, (int)addrResult->ai_addrlen) == SOCKET_ERROR)
	{
		printf("Unable to bind socket on port %s: %ld\n", port, WSAGetLastError());
		return false;
	}

	if (listen(g_serverSocket, SOMAXCONN) == SOCKET_ERROR)
	{
		printf("Unable to listen on socket: %ld\n", WSAGetLastError());
		return false;
	}
	
	char address[48];

	if (!QueryInterfaceAddress(address, sizeof(address)))
		return false;

	g_initVars["SERVER_NAME"] = address;
	g_initVars["SERVER_PORT"] = port;
	g_initVars["HTTPS"] = "off";

	printf("Listening on port %s\n", port);
	return true;
}

DWORD WINAPI HTTPRequestThread(LPVOID lpParam)
{
	SOCKET clientSocket = *reinterpret_cast<SOCKET*>(lpParam);

	HTTPConnection *conn = new HTTPConnection(clientSocket);
	HTTPSession *session = new HTTPSession(g_initVars);

	g_serverConnections.insert(conn);

	int ret;

	do
	{
#ifdef _DEBUG
		puts("Waiting for receive");
#endif

		ret = recv(clientSocket, session->message + session->messageLength, 
			sizeof(session->message) - session->messageLength, 0);
	
#ifdef _DEBUG
		printf("Received %d\n", ret);
#endif

		if (ret > 0)
		{
			session->FeedBuffer(ret);

			if (session->finished)
			{
				conn->StartSession(session);

				EXTENSION_CONTROL_BLOCK ecb = {0};

				ecb.cbSize = sizeof(ecb);
				ecb.dwVersion = 393216; // IIS 6.0
				ecb.dwHttpStatusCode = 500;

				ecb.lpszPathInfo = (LPSTR)"/RBXGS/WebService.dll";
				ecb.lpszPathTranslated = g_modulePath;

				ecb.lpszMethod = (LPSTR)session->method.c_str();
				ecb.lpszQueryString = (LPSTR)session->query.c_str();
				ecb.cbTotalBytes = session->contentLength;
				ecb.cbAvailable = session->contentLength;
				ecb.lpbData = (LPBYTE)(session->contentLength == 0 ? NULL : session->messagePos);
				
				ecb.lpszContentType = (LPSTR)session->contentType.c_str();

				ecb.GetServerVariable = ISAPIGetServerVariable;
				ecb.WriteClient = ISAPIWriteClient;
				ecb.ReadClient = ISAPIReadClient;
				ecb.ServerSupportFunction = ISAPIServerSupportFunction;
				ecb.ConnID = (HCONN)conn;

				WebService::HttpExtensionProc(&ecb);

				session = new HTTPSession(g_initVars);
			}
			else if (session->error)
			{
				conn->SendError(400);
				session->Reset();
			}
		}
		else if (ret == 0)
		{
			if (session->messageLength == sizeof(session->message))
				conn->SendError(413);
		}
		else if (g_running)
		{
			printf("Client socket receive error %d, %d\n", ret, WSAGetLastError());
		}
	} while (ret > 0);

	g_serverConnections.erase(conn);

	delete conn;
	delete session;

	return 0;
}

bool Startup(const char *baseDir, const char *port)
{
	if (!PathIsDirectoryA(baseDir))
	{
		puts("The specified base directory does not exist");
		return false;
	}

	char *files[] = {"OSMESA32.DLL", "OPENGL32.DLL", "GLU32.DLL", "fmodex.dll"};

	PathCombineA(g_modulePath, baseDir, "WebService.dll");
	PathCombineA(g_contentFolderPath, baseDir, "content");

	bool fileMissing = false;

	if (!PathFileExistsA(g_modulePath))
	{
		puts("Could not find WebService.dll");
		fileMissing = true;
	}

	if (!PathFileExistsA(g_contentFolderPath))
	{
		puts("Could not find content folder");
		fileMissing = true;
	}

	for (int i = 0; i < sizeof(files)/sizeof(char*); i++)
	{
		char path[MAX_PATH];
		PathCombineA(path, baseDir, files[i]);

		if (!PathFileExistsA(path))
		{
			printf("Could not find %s\n", files[i]);
			fileMissing = true;
		}
	}

	if (fileMissing)
		return false;

	if (!InitWebService(g_modulePath))
	{
		puts("Could not initialize web service");
		return false;
	}

	InitHooks(g_modulePath, g_contentFolderPath);

	if (!StartHTTPServer(port))
	{
		puts("Could not start HTTP server");
		return false;
	}

	g_running = true;

	return true;
}

void Shutdown()
{
	g_running = false;

	StopWebService();

	for (std::set<HTTPConnection*>::iterator it = g_serverConnections.begin(); it != g_serverConnections.end(); ++it)
		(*it)->Close();

	closesocket(g_serverSocket);
}

BOOL WINAPI ConsoleCtrlHandler(DWORD fdwCtrlType)
{
	switch (fdwCtrlType)
	{
	case CTRL_C_EVENT:
	case CTRL_CLOSE_EVENT:
		puts("Stopping...");
		Shutdown();
		return TRUE;
	}

	return FALSE;
}

int _tmain(int argc, _TCHAR *argv[])
{
	SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
	
	// by default, the base path is the folder the exe is located in
	TCHAR wszBaseDir[MAX_PATH] = TEXT("");
	char szBaseDir[MAX_PATH] = "";
	char szPort[6] = "64989";
	bool overrideBaseDir = false;
	TCHAR** lppPart = {NULL};

	for (int i = 0; i < argc; i++)
	{
		if (wcscmp(argv[i], L"-h") == 0 || wcscmp(argv[i], L"--help") == 0)
		{
			puts("RBXGSConHost v1.2.0 (https://github.com/pizzaboxer/rbxgsconhost)\n");
			puts("Usage:");
			puts("  -h, --help            Print this help message");
			puts("  -p, --port <port>     Specify web server port (default is 64989)");
			puts("  -b, --baseDir <path>  Specify path where RBXGS is located (default is working dir)");
			return 0;
		}

		if (i < argc-1)
		{
			if (wcscmp(argv[i], L"-p") == 0 || wcscmp(argv[i], L"--port") == 0)
			{
				int port = _wtoi(argv[i+1]);

				if (port > 0 && port < 65535)
					_itoa_s(port, szPort, 10);
			}
			else if (wcscmp(argv[i], L"-b") == 0 || wcscmp(argv[i], L"--baseDir") == 0)
			{
				GetFullPathNameW(argv[i+1], MAX_PATH, wszBaseDir, lppPart);
				overrideBaseDir = true;
			}
		}
	}

	if (!overrideBaseDir)
	{
		GetModuleFileName(NULL, wszBaseDir, MAX_PATH);
		PathRemoveFileSpec(wszBaseDir);
	}

	// we'll be handling it in ascii form since any place we use it only really supports ascii anyway
	wcstombs_s(NULL, szBaseDir, wszBaseDir, MAX_PATH-1);

	if (!Startup(szBaseDir, szPort))
	{
		Shutdown();
#ifdef _DEBUG
		getchar();
#endif
		return 1;
	}

	while (g_running)
	{
		SOCKET clientSocket = accept(g_serverSocket, NULL, NULL);

		if (g_running)
		{
#ifdef _DEBUG
			puts("Accepted new connection");
#endif

			if (clientSocket == INVALID_SOCKET)
				printf("accept failed: %d\n", WSAGetLastError());
			else
				CreateThread(NULL, 0, HTTPRequestThread, &clientSocket, 0, NULL);
		}
	}

	return 0;
}
