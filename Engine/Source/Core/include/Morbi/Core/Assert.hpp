#pragma once

#include <cstdlib>
#include "Morbi/Core/Diagnostics/BreadcrumbTracker.h"
#include "Morbi/Core/Log.hpp"

#if defined(_MSC_VER)
    #define MORBI_DEBUG_BREAK() __debugbreak()
#else
    #define MORBI_DEBUG_BREAK() __builtin_trap()
#endif

#define MORBI_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            ::Morbi::Core::Log::Fatal("Assert", message); \
            ::Morbi::Core::Log::Fatal("Assert.Breadcrumbs", ::Morbi::Core::Diagnostics::BreadcrumbTracker::FormatSummary()); \
            ::Morbi::Core::Log::Flush(); \
            MORBI_DEBUG_BREAK(); \
            std::abort(); \
        } \
    } while (false)
