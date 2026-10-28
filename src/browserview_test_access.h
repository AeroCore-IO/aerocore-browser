#pragma once

class BrowserView;

class BrowserViewTestAccess
{
public:
    static void setLoading(BrowserView *view, bool loading);
    static void setHasFrame(BrowserView *view, bool hasFrame);
    static bool notifyNextFrameArmed(const BrowserView *view);
    static void deliverPendingFrameNotification(BrowserView *view);
    static int mouseButtonEventCount(const BrowserView *view);
};
