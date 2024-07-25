// LoaderTest.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include <time.h>
#include <windows.h>
#include <tchar.h>
#include <httpext.h>
#include <iphlpapi.h>
#include <dbghelp.h>
#include <shlwapi.h>

#include "MinHook.h"

#include "HTTPConnection.h"
#include "WebService.h"
#include "RBXDefs.h"

HANDLE g_consoleHandle;
char g_modulePath[MAX_PATH];
char g_serverAddress[48];
char g_serverPort[6] = "64989";
SOCKET g_serverSocket;

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
		// printf("Failed to GetServerVariable %s (index not found)\n", lpszVariableName);
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

	time_t rawtime;
	struct tm *timeinfo;
	char timeHeaderBuf[64];

	time(&rawtime);
	timeinfo = gmtime(&rawtime);

	strftime(timeHeaderBuf, sizeof(timeHeaderBuf), "%a, %d %b %Y %H:%M:%S GMT", timeinfo);

	response << "Date: " << timeHeaderBuf << "\r\n";
	response << "Server: RBXGSConHost\r\n";

	if (strncmp(szBuffer, "<soap:", 6) == 0 || strncmp(szBuffer, "<?xml", 5) == 0)
		response << "Content-Type: text/xml\r\n";

	response << "Content-Length: " << *lpdwBytes << "\r\n";
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
	// WSA is implicitly initialized by RBXGS, seems to be <2.2.0

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

	printf("Listening on port %s\n", g_serverPort);
	return true;
}

void HandleHTTPRequest()
{
	SOCKET clientSocket = accept(g_serverSocket, NULL, NULL);

	if (clientSocket == INVALID_SOCKET)
	{
		if (WebService::Running)
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

	WebService::HttpExtensionProc(&ecb);
}

void __fastcall RBXStandardOutRaisedHook(void *_this, RBX::StandardOutMessage message)
{
	switch (message.type)
	{
		case RBX::MESSAGE_OUTPUT:
			SetConsoleTextAttribute(g_consoleHandle, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
			break;

		case RBX::MESSAGE_INFO:
			SetConsoleTextAttribute(g_consoleHandle, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
			break;

		case RBX::MESSAGE_WARNING:
			SetConsoleTextAttribute(g_consoleHandle, FOREGROUND_RED | FOREGROUND_GREEN);
			break;

		case RBX::MESSAGE_ERROR:
			SetConsoleTextAttribute(g_consoleHandle, FOREGROUND_RED | FOREGROUND_INTENSITY);
			break;
	}

	puts(message.message.c_str());

	SetConsoleTextAttribute(g_consoleHandle, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);

	RBX::StandardOutRaised(_this, message);	
}

bool InitializeSymbolHook()
{
	HANDLE hProcess = GetCurrentProcess();

	SymSetOptions(SYMOPT_DEBUG | SYMOPT_LOAD_ANYTHING);

	if (!SymInitialize(hProcess, NULL, FALSE))
	{
		printf("SymInitialize failed: %d\n", GetLastError());
		return false;
	}

	DWORD moduleBase = SymLoadModuleEx(hProcess, NULL, g_modulePath, NULL, 0, 0, NULL, 0);
	if (moduleBase == 0)
	{
		printf("SymLoadModuleEx failed: %d\n", GetLastError());
		return false;
	}

	IMAGEHLP_MODULE moduleInfo = {0};
	moduleInfo.SizeOfStruct = sizeof(moduleInfo);
	if (!SymGetModuleInfo(hProcess, moduleBase, &moduleInfo))
	{
    	printf("SymGetModuleInfo failed: %d\n", GetLastError());
    	return false;
	}

	if (moduleInfo.SymType != SYM_TYPE::SymPdb)
	{
		puts("Could not find WebService.pdb");
		return false;
	}

	// void __thiscall RBX::Notifier<RBX::StandardOut,RBX::StandardOutMessage>::raise(
	//         RBX::Notifier<RBX::StandardOut,RBX::StandardOutMessage> *this,
	//         RBX::StandardOutMessage event)

	// versions of windows that are too old will not be able to read the symbols (for some reason)
	// only version i've tested this on is server 2003

	IMAGEHLP_SYMBOL symbol = {0};
	if (!SymGetSymFromName(hProcess, "?raise@?$Notifier@VStandardOut@RBX@@UStandardOutMessage@2@@RBX@@IBEXUStandardOutMessage@2@@Z", &symbol))
	{
		printf("SymGetSymFromName failed: %d\n", GetLastError());
		puts("This may be because the version of Windows is too old");
		return false;
	}

	if (MH_Initialize() != MH_OK)
	{
		puts("Failed to initialize MinHook");
        return false;
	}

	if (MH_CreateHook((LPVOID)symbol.Address, RBXStandardOutRaisedHook, reinterpret_cast<LPVOID*>(&RBX::StandardOutRaised)) != MH_OK)
	{
		puts("Failed to create hook");
		return false;
	}

	if (MH_EnableHook((LPVOID)symbol.Address) != MH_OK)
	{
		puts("Failed to enable hook");
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

void Cleanup()
{
	WebService::Stop();
	closesocket(g_serverSocket);
	WSACleanup();
}

BOOL WINAPI ConsoleCtrlHandler(DWORD fdwCtrlType)
{
	switch (fdwCtrlType)
	{
	case CTRL_C_EVENT:
	case CTRL_CLOSE_EVENT:
		puts("Stopping...");
		Cleanup();
		return TRUE;
	}

	return FALSE;
}

bool StartupSequence()
{
	if (!WebService::Initialize(g_modulePath))
	{
		puts("Could not initialize web service");
		return false;
	}

	if (!InitializeSymbolHook())
	{
		puts("StandardOut redirection will not apply");
		return false;
	}

	if (!QueryServerAddress())
		return false;

	if (!StartHTTPServer(g_serverPort))
	{
		puts("Could not start HTTP server");
		return false;
	}

	return true;
}

int _tmain(int argc, _TCHAR *argv[])
{
	g_consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);

	SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
	
	// by default, the base path is the folder the exe is located in
	TCHAR *wszBaseDir = NULL;
	char szBaseDir[MAX_PATH];

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
			else if (wcscmp(argv[i], L"-b") == 0 || wcscmp(argv[i], L"--baseDir") == 0)
			{
				wszBaseDir = argv[i+1];
			}
		}
	}

	// we'll be handling it in ascii form since any place we use it only supports ascii anyway
	if (wszBaseDir == NULL)
	{
		wszBaseDir = (TCHAR*)malloc(MAX_PATH);
		GetModuleFileName(NULL, wszBaseDir, MAX_PATH);
		PathRemoveFileSpec(wszBaseDir);
	}
	else if (!PathIsDirectory(wszBaseDir))
	{
		puts("The specified base directory does not exist");
		return 1;
	}

	wcstombs_s(NULL, szBaseDir, wszBaseDir, MAX_PATH-1);

	PathCombineA(g_modulePath, szBaseDir, "WebService.dll");

	bool dllMissing = false;

	if (!PathFileExistsA(g_modulePath))
	{
		puts("Could not find WebService.dll");
		dllMissing = true;
	}

	char *files[] = {"OSMESA32.DLL", "OPENGL32.DLL", "GLU32.DLL", "fmodex.dll"};

	for (int i = 0; i < sizeof(files)/sizeof(char*); i++)
	{
		char path[256];
		PathCombineA(path, szBaseDir, files[i]);

		if (!PathFileExistsA(path))
		{
			printf("Could not find %s\n", files[i]);
			dllMissing = true;
		}
	}

	if (dllMissing || !StartupSequence())
	{
		Cleanup();
		getchar();
		return 1;
	}

	while (WebService::Running)
	{
		HandleHTTPRequest();
	}

	return 0;
}
