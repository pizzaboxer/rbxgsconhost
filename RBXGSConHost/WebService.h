#pragma once

namespace WebService
{
    typedef BOOL(WINAPI *GetExtensionVersion_t)(HSE_VERSION_INFO *);
    typedef DWORD(WINAPI *HttpExtensionProc_t)(EXTENSION_CONTROL_BLOCK *);
    typedef BOOL(WINAPI *TerminateExtension_t)(DWORD);

    extern GetExtensionVersion_t GetExtensionVersion;
    extern HttpExtensionProc_t HttpExtensionProc;
    extern TerminateExtension_t TerminateExtension;

    extern bool Running;

    bool Initialize(char *modulePath);
    void Stop();
}