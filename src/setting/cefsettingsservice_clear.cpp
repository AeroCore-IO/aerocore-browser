#include "setting/cefsettingsservice.h"

#include "include/cef_callback.h"
#include "include/cef_cookie.h"
#include "include/cef_request_context.h"

#include <QDebug>

namespace {

// CEF 151 ClearHttpCache binds callback->OnComplete with base::BindOnce.
// A null callback is dereferenced when the work finishes — including
// immediately when the cache is empty (home page, no browser yet).
// CefSharp documents the same crash for the sibling API
// ClearHttpAuthCredentials. Always pass a live callback.
class NoopCompletionCallback final : public CefCompletionCallback
{
public:
    void OnComplete() override {}

private:
    IMPLEMENT_REFCOUNTING(NoopCompletionCallback);
};

class NoopDeleteCookiesCallback final : public CefDeleteCookiesCallback
{
public:
    void OnComplete(int) override {}

private:
    IMPLEMENT_REFCOUNTING(NoopDeleteCookiesCallback);
};

} // namespace

void CefSettingsService::clearCefCookies()
{
    CefRefPtr<CefCookieManager> manager = CefCookieManager::GetGlobalManager(nullptr);
    if (!manager) {
        qWarning() << "clearCefCookies: global cookie manager unavailable";
        return;
    }

    manager->DeleteCookies(CefString(), CefString(), new NoopDeleteCookiesCallback());
}

void CefSettingsService::clearCefCache()
{
    CefRefPtr<CefRequestContext> context = CefRequestContext::GetGlobalContext();
    if (!context) {
        qWarning() << "clearCefCache: global request context unavailable";
        return;
    }

    qInfo() << "clearCefCache: requesting HTTP cache clear";
    context->ClearHttpCache(new NoopCompletionCallback());
}
