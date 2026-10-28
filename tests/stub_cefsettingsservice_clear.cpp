#include "setting/cefsettingsservice.h"

// Production clear lives in src/setting/cefsettingsservice_clear.cpp and must
// never pass a null CefCompletionCallback into CEF (null -> SIGSEGV).

void CefSettingsService::clearCefCookies()
{
}

void CefSettingsService::clearCefCache()
{
}
