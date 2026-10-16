// Background work on the Windows thread pool + marshaling results back to the UI thread.
#pragma once
#include "common.h"

#define WM_APP_MAINQUEUE (WM_APP + 1)

void JobsInit(HWND notifyWindow);
void JobsShutdown();                          // waits for outstanding jobs
void JobSubmit(std::function<void()> work);   // runs on a pool thread (COM MTA initialized)
void MainPost(std::function<void()> fn);      // runs on UI thread during MainDrain()
void MainDrain();
int  JobsPending();
