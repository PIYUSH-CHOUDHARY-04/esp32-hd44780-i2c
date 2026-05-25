#ifndef _LOGGING_H__
#define _LOGGING_H__

#include <stdbool.h>



#define LOG_MSGS    true
#if LOG_MSGS
    #define PRINT_MSG(fmt, ...) printf("[DEBUG] " fmt,  ##__VA_ARGS__)
#else
    #define PRINT_MSG(fmt, ...) ((void)0)
#endif
   


#endif  /* __LOGGING_H__ */
