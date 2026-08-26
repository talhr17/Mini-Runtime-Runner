#pragma once

#include <sys/types.h> 

typedef struct {
    char id[64];               
    char cmd[256];         
    unsigned int timeout_ms; 

    char status[16];         
    int exit_code;
    int term_signal;
    int duration_ms;
} Job;