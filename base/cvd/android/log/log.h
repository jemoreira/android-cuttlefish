/*
 * Copyright 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <android/log.h>

#ifndef LOG_NDEBUG
#ifdef NDEBUG
#define LOG_NDEBUG 1
#else
#define LOG_NDEBUG 0
#endif
#endif


/*
 * Basic log message macros intended to emulate the behavior of log/log.h
 * in system core.  This should be dependent only on ndk exposed logging
 * functionality.
 */

#ifndef ALOG
#define ALOG(priority, tag, fmt, ...) 0
#endif

#ifndef ALOGV
#if LOG_NDEBUG
#define ALOGV(...)   ((void)0)
#else
#define ALOGV(...) ((void)ALOG(LOG_VERBOSE, LOG_TAG, __VA_ARGS__))
#endif
#endif

#ifndef ALOGD
#define ALOGD(...) ((void)ALOG(LOG_DEBUG, LOG_TAG, __VA_ARGS__))
#endif

#ifndef ALOGI
#define ALOGI(...) ((void)ALOG(LOG_INFO, LOG_TAG, __VA_ARGS__))
#endif

#ifndef ALOGW
#define ALOGW(...) ((void)ALOG(LOG_WARN, LOG_TAG, __VA_ARGS__))
#endif

#ifndef ALOGE
#define ALOGE(...) ((void)ALOG(LOG_ERROR, LOG_TAG, __VA_ARGS__))
#endif

#ifndef ALOGF
#define ALOGF(...) ((void)ALOG(LOG_FATAL, LOG_TAG, __VA_ARGS__))
#endif

/*
 * Log a fatal error if cond is true. The condition test is inverted from
 * assert(3) semantics. The test and message are not stripped from release
 * builds
 */
#ifndef ALOG_ALWAYS_FATAL_IF
#define ALOG_ALWAYS_FATAL_IF(cond, ...) \
    if (cond) __android_log_assert(#cond, LOG_TAG, __VA_ARGS__)
#endif

