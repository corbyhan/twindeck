#include "jobs.h"
#include <deque>

static HWND g_notify = nullptr;
static std::mutex g_mainMutex;
static std::deque<std::function<void()>> g_mainQueue;
static std::atomic<int> g_pending{0};
static std::atomic<bool> g_posted{false};
static PTP_POOL g_pool = nullptr;
static TP_CALLBACK_ENVIRON g_env;
static PTP_CLEANUP_GROUP g_cleanup = nullptr;

void JobsInit(HWND notify) {
    g_notify = notify;
    g_pool = CreateThreadpool(nullptr);
    if (g_pool) {
        SYSTEM_INFO si; GetSystemInfo(&si);
        DWORD n = std::max<DWORD>(2, si.dwNumberOfProcessors);
        SetThreadpoolThreadMinimum(g_pool, 2);
        SetThreadpoolThreadMaximum(g_pool, n * 2);
        InitializeThreadpoolEnvironment(&g_env);
        SetThreadpoolCallbackPool(&g_env, g_pool);
        g_cleanup = CreateThreadpoolCleanupGroup();
        SetThreadpoolCallbackCleanupGroup(&g_env, g_cleanup, nullptr);
    }
}

void JobsShutdown() {
    // wait (bounded) for running jobs before exiting
    DWORD wait = 20;
    for (int i = 0; i < 50 && g_pending.load() > 0; i++) {
        Sleep(wait);
        wait = std::min<DWORD>(100, wait + 20);
    }
    if (g_cleanup) {
        CloseThreadpoolCleanupGroupMembers(g_cleanup, TRUE, nullptr);
        CloseThreadpoolCleanupGroup(g_cleanup);
        g_cleanup = nullptr;
    }
    if (g_pool) {
        DestroyThreadpoolEnvironment(&g_env);
        CloseThreadpool(g_pool);
        g_pool = nullptr;
    }
}

static void CALLBACK WorkCallback(PTP_CALLBACK_INSTANCE, PVOID ctx) {
    thread_local bool comInit = false;
    if (!comInit) {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        comInit = true;
    }
    auto *fn = (std::function<void()> *)ctx;
    try {
        (*fn)();
    } catch (...) {
        Log(L"Job threw an exception");
    }
    delete fn;
    g_pending--;
}

void JobSubmit(std::function<void()> work) {
    g_pending++;
    auto *fn = new std::function<void()>(std::move(work));
    if (!g_pool || !TrySubmitThreadpoolCallback(WorkCallback, fn, &g_env)) {
        WorkCallback(nullptr, fn); // run inline as fallback
    }
}

void MainPost(std::function<void()> fn) {
    {
        std::lock_guard<std::mutex> lk(g_mainMutex);
        g_mainQueue.push_back(std::move(fn));
    }
    if (!g_posted.exchange(true) && g_notify) PostMessageW(g_notify, WM_APP_MAINQUEUE, 0, 0);
}

void MainDrain() {
    g_posted = false;
    for (;;) {
        std::deque<std::function<void()>> q;
        {
            std::lock_guard<std::mutex> lk(g_mainMutex);
            q.swap(g_mainQueue);
        }
        if (q.empty()) break;
        for (auto &f : q) f();
    }
}

int JobsPending() { return g_pending.load(); }
