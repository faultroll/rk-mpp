/*
 * Copyright 2015 Rockchip Electronics Co. LTD
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

#include <stdarg.h> // va_list
#include "os_log.h"

#if defined(linux) && !defined(__ANDROID__)
// From osal/windows/os_log.c
#include <stdio.h> // snprintf
#include <stdarg.h> // vfprintf

#define LINE_SZ 1024

void os_log_info(const char* tag, const char* msg, va_list list)
{
    char line[LINE_SZ] = {0};
    snprintf(line, sizeof(line), "%s: %s", tag, msg);
    vfprintf(stdout, line, list);
}

void os_log_error(const char* tag, const char* msg, va_list list)
{
    char line[LINE_SZ] = {0};
    snprintf(line, sizeof(line), "%s: %s", tag, msg);
    vfprintf(stderr, line, list);
}

void os_log_trace(const char* tag, const char* msg, va_list list)
{
    os_log_info(tag, msg, list);
}

void os_log_debug(const char* tag, const char* msg, va_list list)
{
    os_log_info(tag, msg, list);
}

void os_log_warn(const char* tag, const char* msg, va_list list)
{
    os_log_error(tag, msg, list);
}

void os_log_fatal(const char* tag, const char* msg, va_list list)
{
    os_log_error(tag, msg, list);
}

#elif 0 // disable syslog
#include <stdio.h>
#include <stdarg.h>
#include <syslog.h>

#include "os_env.h" // os_get_env_u32

#define LINE_SZ 1024

class SyslogWrapper
{
private:
    // avoid any unwanted function
    SyslogWrapper(const SyslogWrapper &);
    SyslogWrapper &operator=(const SyslogWrapper &);
public:
    SyslogWrapper();
    ~SyslogWrapper();
};

static SyslogWrapper syslog_wrapper;

SyslogWrapper::SyslogWrapper()
{
    int option = LOG_PID | LOG_CONS;
    RK_U32 syslog_perror = 0;

    os_get_env_u32("mpp_syslog_perror", &syslog_perror, 0);
    if (syslog_perror)
        option |= LOG_PERROR;

    openlog("mpp", option, LOG_USER);
}

SyslogWrapper::~SyslogWrapper()
{
    closelog();
}

void os_log_trace(const char* tag, const char* msg, va_list list)
{
    char line[LINE_SZ] = {0};
    snprintf(line, sizeof(line) - 1, "%s: %s", tag, msg);
    vsyslog(LOG_NOTICE, line, list);
}

void os_log_debug(const char* tag, const char* msg, va_list list)
{
    char line[LINE_SZ] = {0};
    snprintf(line, sizeof(line) - 1, "%s: %s", tag, msg);
    vsyslog(LOG_DEBUG, line, list);
}

void os_log_info(const char* tag, const char* msg, va_list list)
{
    char line[LINE_SZ] = {0};
    snprintf(line, sizeof(line) - 1, "%s: %s", tag, msg);
    vsyslog(LOG_INFO, line, list);
}

void os_log_warn(const char* tag, const char* msg, va_list list)
{
    char line[LINE_SZ] = {0};
    snprintf(line, sizeof(line) - 1, "%s: %s", tag, msg);
    vsyslog(LOG_WARNING, line, list);
}

void os_log_error(const char* tag, const char* msg, va_list list)
{
    char line[LINE_SZ] = {0};
    snprintf(line, sizeof(line) - 1, "%s: %s", tag, msg);
    vsyslog(LOG_ERR, line, list);
}

void os_log_fatal(const char* tag, const char* msg, va_list list)
{
    char line[LINE_SZ] = {0};
    snprintf(line, sizeof(line) - 1, "%s: %s", tag, msg);
    vsyslog(LOG_CRIT, line, list);
}

#endif
