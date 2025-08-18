#ifndef _CONNECT_H_
#define _CONNECT_H_
#include <nlohmann/json.hpp>
using nlohmann_json = nlohmann::json;
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


int initWiFi();
int initHttpd();
int deinitHttpd();
void initConnectivity();

int stopWifi();
void process_detection_results(nlohmann_json& parsed, uint8_t* frame,std::chrono::steady_clock::time_point& last_helmet_alert, std::chrono::steady_clock::time_point& last_zone_alert,std::chrono::steady_clock::time_point& last_person_report) ;

#ifdef __cplusplus
}
#endif

#endif // _CONNECT_H_