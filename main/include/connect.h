#ifndef _CONNECT_H_
#define _CONNECT_H_

#include "global_cfg.h"

// --- Configuración general ---
#define HTTPS_SUPPORT 1

#ifdef __cplusplus
// Solo C++ entiende enum class
enum class ConnectivityMode {
    NONE,
    MQTT,
    HTTP_TO_ESP
};

// Variable global
extern ConnectivityMode connectivity_mode;
#endif

#ifdef __cplusplus
extern "C" {
#endif

// --- Funciones C ---
int initHttpd();
int deinitHttpd();
void initConnectivity();
int initWiFi();
int stopWifi();
int check_internet_connection();

#ifdef __cplusplus
}
#endif

#endif // _CONNECT_H_