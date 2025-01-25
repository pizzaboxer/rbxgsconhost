#pragma once

typedef BOOL(WINAPI *GetExtensionVersion_t)(HSE_VERSION_INFO *);
typedef DWORD(WINAPI *HttpExtensionProc_t)(EXTENSION_CONTROL_BLOCK *);
typedef BOOL(WINAPI *TerminateExtension_t)(DWORD);

namespace WebService
{
	extern GetExtensionVersion_t GetExtensionVersion;
	extern HttpExtensionProc_t HttpExtensionProc;
	extern TerminateExtension_t TerminateExtension;
}

bool InitWebService(char *modulePath);
void StopWebService();