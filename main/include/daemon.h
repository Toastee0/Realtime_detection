#ifndef _DAEMON_H_
#define _DAEMON_H_

#include <thread>
#include "hv/HttpServer.h"
#include "hv/hasync.h"   // import hv::async
#include "hv/hthread.h"  // import hv_gettid
enum APP_STATUS {
    APP_STATUS_NORMAL,
    APP_STATUS_STOP,
    APP_STATUS_NORESPONSE,
    APP_STATUS_STARTFAILED,
    APP_STATUS_UNKNOWN
};

extern int noderedStarting;
extern int sscmaStarting;
extern APP_STATUS noderedStatus;
extern APP_STATUS sscmaStatus;

int stopFlow();
void initDaemon();
void stopDaemon();

#endif
