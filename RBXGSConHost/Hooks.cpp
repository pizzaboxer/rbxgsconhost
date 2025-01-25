#include "stdafx.h"
#include "Hooks.h"

#include <iostream>
#include <dbghelp.h>
#include <shlobj.h>

#include "MinHook.h"

#include "RBXDefs.h"

static HANDLE hProcess = GetCurrentProcess();
const char *g_contentFolderPath;

HRESULT (WINAPI *SHGetFolderPathAndSubDirA_fp)(HWND, int, HANDLE, DWORD, LPCSTR, LPSTR);
HRESULT WINAPI SHGetFolderPathAndSubDirA_hook(HWND hWnd, int csidl, HANDLE hToken, DWORD dwFlags, LPCSTR pszSubDir, LPSTR pszPath)
{
	if (csidl == CSIDL_COMMON_APPDATA && strcmp(pszSubDir, "Roblox\\content\\") == 0)
	{
		strcpy_s(pszPath, MAX_PATH, g_contentFolderPath);
		return S_OK;
	}

	return SHGetFolderPathAndSubDirA_fp(hWnd, csidl, hToken, dwFlags, pszSubDir, pszPath);
}

// void __thiscall RBX::Notifier<RBX::StandardOut,RBX::StandardOutMessage>::raise(
//         RBX::Notifier<RBX::StandardOut,RBX::StandardOutMessage> *this,
//         RBX::StandardOutMessage event)
// we're hooking this because RBX::StandardOut::print uses varargs and yeah i didn't feel like doing that lol
const PSTR StandardOutRaised_sym = "?raise@?$Notifier@VStandardOut@RBX@@UStandardOutMessage@2@@RBX@@IBEXUStandardOutMessage@2@@Z";
void (__thiscall *StandardOutRaised_fp)(void *, RBX::StandardOutMessage);
void __fastcall StandardOutRaised_hook(void *_this, RBX::StandardOutMessage message)
{
	HANDLE consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);

	switch (message.type)
	{
	case RBX::MESSAGE_OUTPUT:
		SetConsoleTextAttribute(consoleHandle, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
		break;

	case RBX::MESSAGE_INFO:
		SetConsoleTextAttribute(consoleHandle, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
		break;

	case RBX::MESSAGE_WARNING:
		SetConsoleTextAttribute(consoleHandle, FOREGROUND_RED | FOREGROUND_GREEN);
		break;

	case RBX::MESSAGE_ERROR:
		SetConsoleTextAttribute(consoleHandle, FOREGROUND_RED | FOREGROUND_INTENSITY);
		break;
	}

	std::cout << message.message << "\n";

	SetConsoleTextAttribute(consoleHandle, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);

	StandardOutRaised_fp(_this, message);	
}

bool InstallHook(LPVOID pTarget, LPVOID pDetour, LPVOID *ppOriginal)
{
	if (MH_CreateHook(pTarget, pDetour, ppOriginal) != MH_OK)
		return false;

	if (MH_EnableHook(pTarget) != MH_OK)
		return false;

	return true;
}

bool InstallHook(PSTR pSymbolName, LPVOID pDetour, LPVOID *ppOriginal)
{
	IMAGEHLP_SYMBOL symbol = {0};

	if (!SymGetSymFromName(hProcess, pSymbolName, &symbol))
		return false;

	return InstallHook(reinterpret_cast<LPVOID>(symbol.Address), pDetour, ppOriginal);
}

bool InitSymbols(const char *modulePath)
{
	SymSetOptions(SYMOPT_LOAD_ANYTHING);

	if (!SymInitialize(hProcess, NULL, FALSE))
	{
		printf("SymInitialize failed: %d\n", GetLastError());
		return false;
	}

	DWORD moduleBase = SymLoadModuleEx(hProcess, NULL, (PSTR)modulePath, NULL, 0, 0, NULL, 0);
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

	if (moduleInfo.SymType != SymPdb)
	{
		puts("Could not find WebService.pdb");
		return false;
	}

	return true;
}

void InitHooks(const char *modulePath, const char *contentFolderPath)
{
	g_contentFolderPath = contentFolderPath;

	if (MH_Initialize() != MH_OK)
	{
		puts("Failed to initialize MinHook");
		puts("Content folder and StandardOut redirection will not apply");
		return;
	}

	// versions of windows that are too old will not be able to read the symbols (for some reason)
	// only version i've tested this on is server 2003
	if (!InitSymbols(modulePath))
		puts("Failed to load symbols");

	if (!InstallHook(SHGetFolderPathAndSubDirA, SHGetFolderPathAndSubDirA_hook, reinterpret_cast<LPVOID*>(&SHGetFolderPathAndSubDirA_fp)))
		puts("Content folder redirection failed");

	if (!InstallHook(StandardOutRaised_sym, StandardOutRaised_hook, reinterpret_cast<LPVOID*>(&StandardOutRaised_fp)))
		puts("StandardOut redirection failed");
}
