// Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
#include "process-stats.h"
#include <cstdio>
#include <cstring>
#include <sys/resource.h>
#include <time.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#endif

namespace
{
bool processStats(JSContext* cx, unsigned argc, JS::Value* vp)
{
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    if (argc != 0)
    {
        JS_ReportError(cx, "__jsbComparisonProcessStats takes no arguments");
        return false;
    }
    struct timespec clock;
    struct rusage usage;
    if (clock_gettime(CLOCK_MONOTONIC, &clock) != 0 || getrusage(RUSAGE_SELF, &usage) != 0)
    {
        JS_ReportError(cx, "Process timing measurement failed");
        return false;
    }
    long rssKiB = -1;
#if defined(__APPLE__)
    mach_task_basic_info_data_t taskInfo{};
    mach_msg_type_number_t infoCount = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&taskInfo), &infoCount) ==
        KERN_SUCCESS)
    {
        rssKiB = static_cast<long>(taskInfo.resident_size / 1024);
    }
#else
    if (FILE* file = std::fopen("/proc/self/status", "r"))
    {
        char line[256];
        while (std::fgets(line, sizeof(line), file))
        {
            if (std::strncmp(line, "VmRSS:", 6) == 0 && std::sscanf(line + 6, "%ld", &rssKiB) == 1)
                break;
        }
        std::fclose(file);
    }
#endif
    JS::RootedObject result(cx, JS_NewObject(cx, nullptr, JS::NullPtr(), JS::NullPtr()));
    if (!result)
        return false;
    struct Entry { const char* name; double value; };
    const Entry entries[] = {
        {"monotonic_ms", clock.tv_sec * 1000.0 + clock.tv_nsec / 1000000.0},
        {"user_ms", usage.ru_utime.tv_sec * 1000.0 + usage.ru_utime.tv_usec / 1000.0},
        {"system_ms", usage.ru_stime.tv_sec * 1000.0 + usage.ru_stime.tv_usec / 1000.0},
        {"rss_kib", static_cast<double>(rssKiB)},
        {"voluntary_context_switches", static_cast<double>(usage.ru_nvcsw)},
        {"involuntary_context_switches", static_cast<double>(usage.ru_nivcsw)}};
    for (const auto& entry : entries)
    {
        JS::RootedValue value(cx, JS::DoubleValue(entry.value));
        if (!JS_DefineProperty(cx, result, entry.name, value, JSPROP_ENUMERATE | JSPROP_READONLY))
            return false;
    }
    args.rval().setObject(*result);
    return true;
}
}

bool registerJsbComparisonProcessStats(JSContext* cx, JS::HandleObject global)
{
    return JS_DefineFunction(cx, global, "__jsbComparisonProcessStats", processStats, 0,
                             JSPROP_READONLY | JSPROP_PERMANENT) != nullptr;
}
