#pragma once

#include <cxxabi.h>
#include <unwind.h>
#include <array>
#include <dlfcn.h>
#include <signal.h>

extern uintptr_t g_sampLibBase;
extern uintptr_t g_gtasaLibBase;

void crash_log(const char* format, ...);
void dump_register(int signal, siginfo_t* info, void* ctx);
void dump_stack(int ignoreDepth = 0, int maxDepth = 31);
// Async-signal-safe crash breadcrumb: snprintf + __android_log_write only.
// Safe to call from a fatal-signal handler; dump_register/dump_stack are NOT.
void crash_breadcrumb(int signal, siginfo_t* info, void* ctx);
