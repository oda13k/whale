
#ifndef _WHALE_LOG_H
#define _WHALE_LOG_H

typedef enum
{
    FATAL = 0,
    ERR,
    WARN,
    INFO,
    DEBUG
} LogLevel;

int wh_log(LogLevel lvl, const char* fmt, ...);

#endif // !_WHALE_LOG_H

