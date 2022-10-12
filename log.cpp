#include "log.h"
char *EM_logLevelGet(const int level){
    if(level == LOGLEVEL_DEBUG){
        return (char*)"DEBUG";
    }else if (level == LOGLEVEL_INFO ){
        return (char*)"INFO";
    }else if (level == LOGLEVEL_WARN ){
        return (char*)"WARN";
    }else if (level == LOGLEVEL_ERROR ){
        return (char*)"ERROR";
    }else{
        return (char*)"UNKNOWN";
    }
    
}

void EM_log(const int level, const char* fun, const int line, const char *fmt, ...){
    #ifdef OPEN_LOG    
    va_list arg;
    va_start(arg, fmt);
    char buf[1024];     
    vsnprintf(buf, sizeof(buf), fmt, arg);        
    va_end(arg);   
    if(level >= LOG_LEVEL){                         
        printf("[%s]\t[%s %d]: %s \n", EM_logLevelGet(level), fun, line, buf);
    }  
    #endif
}