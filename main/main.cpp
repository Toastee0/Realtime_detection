#include <iostream>
#include <signal.h>
#include <string>
#include <syslog.h>
#include <unistd.h>

#include "version.h"

#include "daemon.h"
#include "connect.h"
#include "hv/hlog.h"

const char* ca_cert= "../cam_1/AmazonRootCA1.pem";
const char* cert= "../cam_1/cam_1-certificate.pem.crt";
const char* key= "../cam_1/cam_1-private.pem.key";

const char* aws_endpoint = "a265m6rkc34opn-ats.iot.us-east-1.amazonaws.com";
const char* cli_id = "cam_1";

// cuando inicio la verifico conexión a internet
// si hay conexión a internet, entonces inicio el mqtt
// si no hay conexión a internet, entonces inicio access point a la esp32 y mando la información por http

static void exitHandle(int signo) {
    stopDaemon();
    deinitHttpd();
    stopWifi();
    closelog();

    exit(0);
}

static void initSupervisor() {
    hlog_disable();
    openlog("supervisor", LOG_CONS | LOG_PERROR, LOG_DAEMON);

    initWiFi();
    initHttpd();
    initDaemon();

    initConnectivity();


    signal(SIGINT, &exitHandle);
    signal(SIGTERM, &exitHandle);

    setlogmask(LOG_UPTO(LOG_NOTICE));
}

int main(int argc, char** argv) {

    printf("Build Time: %s %s\n", __DATE__, __TIME__);
    if (argc > 1 && std::string(argv[1]) == "-v") {
        printf("Version: %s\n", PROJECT_VERSION);
    }

    initSupervisor();

    while (1)
        sleep(1000);

    system(SCRIPT_WIFI_STOP);
    deinitHttpd();

    closelog();

    return 0;
}
