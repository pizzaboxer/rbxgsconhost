#include "stdafx.h"
#include "WebService.h"

bool WebService::Running = false;

WebService::GetExtensionVersion_t WebService::GetExtensionVersion;
WebService::HttpExtensionProc_t WebService::HttpExtensionProc;
WebService::TerminateExtension_t WebService::TerminateExtension;

bool WebService::Initialize(char *modulePath)
{    
	HMODULE hModule = LoadLibraryExA(modulePath, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);

	if (hModule == FALSE)
	{
		printf("Could not get Web service module handle: %d\n", GetLastError());
		return false;
	}

	GetExtensionVersion = (GetExtensionVersion_t)GetProcAddress(hModule, "GetExtensionVersion");
	if (GetExtensionVersion == NULL)
	{
		printf("Could not get proc address for GetExtensionVersion: %d\n", GetLastError());
		return false;
	}

	HttpExtensionProc = (HttpExtensionProc_t)GetProcAddress(hModule, "HttpExtensionProc");
	if (HttpExtensionProc == NULL)
	{
		printf("Could not get proc address for HttpExtensionProc: %d\n", GetLastError());
		return false;
	}

	TerminateExtension = (TerminateExtension_t)GetProcAddress(hModule, "TerminateExtension");
	if (TerminateExtension == NULL)
	{
		printf("Could not get proc address for TerminateExtension: %d\n", GetLastError());
		return false;
	}

	// calling GetExtensionVersion is necessary for initialization
	HSE_VERSION_INFO versionInfo = {0};
	GetExtensionVersion(&versionInfo);
	printf("Loading %s\n", versionInfo.lpszExtensionDesc);

	Running = true;

	return true;
}

void WebService::Stop()
{
	if (Running && TerminateExtension != NULL)
		TerminateExtension(HSE_TERM_MUST_UNLOAD);
        
	Running = false;
}