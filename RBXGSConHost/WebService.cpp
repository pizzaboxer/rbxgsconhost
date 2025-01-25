#include "stdafx.h"
#include "WebService.h"

namespace WebService
{
	GetExtensionVersion_t GetExtensionVersion;
	HttpExtensionProc_t HttpExtensionProc;
	TerminateExtension_t TerminateExtension;
}

bool InitWebService(char *modulePath)
{    
	HMODULE hModule = LoadLibraryExA(modulePath, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);

	if (hModule == FALSE)
	{
		printf("Could not get Web service module handle: %d\n", GetLastError());
		return false;
	}

	WebService::GetExtensionVersion = (GetExtensionVersion_t)GetProcAddress(hModule, "GetExtensionVersion");
	if (WebService::GetExtensionVersion == NULL)
	{
		printf("Could not get proc address for GetExtensionVersion: %d\n", GetLastError());
		return false;
	}

	WebService::HttpExtensionProc = (HttpExtensionProc_t)GetProcAddress(hModule, "HttpExtensionProc");
	if (WebService::HttpExtensionProc == NULL)
	{
		printf("Could not get proc address for HttpExtensionProc: %d\n", GetLastError());
		return false;
	}

	WebService::TerminateExtension = (TerminateExtension_t)GetProcAddress(hModule, "TerminateExtension");
	if (WebService::TerminateExtension == NULL)
	{
		printf("Could not get proc address for TerminateExtension: %d\n", GetLastError());
		return false;
	}

	// calling GetExtensionVersion is necessary for initialization
	HSE_VERSION_INFO versionInfo = {0};
	WebService::GetExtensionVersion(&versionInfo);
	printf("Loaded %s\n", versionInfo.lpszExtensionDesc);

	return true;
}

void StopWebService()
{
	if (WebService::TerminateExtension != NULL)
		WebService::TerminateExtension(HSE_TERM_MUST_UNLOAD);
}