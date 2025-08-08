#ifndef _CONNECT_H_
#define _CONNECT_H_

#include "global_cfg.h"

#define HTTPS_SUPPORT 1

#ifdef __cplusplus
enum class ConnectivityMode {
    NONE,
    MQTT,
    HTTP
};

// Variable global
extern ConnectivityMode connectivity_mode;
#endif

#ifdef __cplusplus
extern "C" {
#endif

int initHttpd();
int deinitHttpd();
void initConnectivity();
int initWiFi();
int stopWifi();


#ifdef __cplusplus
}
#endif

#endif // _CONNECT_H_