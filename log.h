#ifndef LOG_H
#define LOG_H

#include<stdarg.h>
#include <stdio.h>
#include "locker.h"
#include "time_check.h"
#include "http_conn.h"

#define OPEN_LOG 1
#define LOG_LEVEL LOGLEVEL_INFO
#define LOG_SAVE 0

typedef enum{                       // log level, big number means higher level
    LOGLEVEL_DEBUG = 0,
    LOGLEVEL_INFO,
    LOGLEVEL_WARN,
    LOGLEVEL_ERROR,
}E_LOGLEVEL;


void EM_log(const int level, const char* fun, const int line, const char *fmt, ...);

#define EMlog(level, fmt...) EM_log(level, __FUNCTION__, __LINE__, fmt)

#endif