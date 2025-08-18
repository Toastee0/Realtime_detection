#include <thread>       // 解决std::this_thread错误
#include <mutex>
#include <memory>
#include <sscma.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <syslog.h>
#include <model_detector.h>
#include "hv/HttpServer.h"
#include "hv/hasync.h"   // import hv::async
#include "hv/hthread.h"  // import hv_gettid
#include "hv/requests.h" 
#include "common.h"
#include "connect.h"
#include "daemon.h"
#include "utils_device.h"
#include "utils_file.h"
#include "utils_led.h"
#include "utils_user.h"
#include "utils_wifi.h"
#include "version.h"
#include "frame_builder.h"
#include "hv/mqtt_client.h"
#include "hv/hssl.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>
#include <atomic>
#include "cvi_sys.h"
#include "cvi_comm_video.h"

using nlohmann_json = nlohmann::json;
using namespace hv;
ConnectivityMode connectivity_mode = ConnectivityMode::MQTT;
hv::MqttClient cli;

uint64_t getUptime() {
    std::ifstream uptime_file("/proc/uptime");
    if (uptime_file.is_open()) {
        double uptime_seconds;
        uptime_file >> uptime_seconds;
        return static_cast<uint64_t>(uptime_seconds * 1000);
    } else {
        std::cerr << "Failed to open /proc/uptime." << std::endl;
        return 0;
    }
}

uint64_t getTimestamp() {
    auto now       = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch());
    return timestamp.count();
}


void initMqtt() {
    hssl_ctx_opt_t opt;
    memset(&opt, 0, sizeof(opt));
    opt.ca_file   = "/etc/ssl/certs/cam_1/AmazonRootCA1.pem";
    opt.crt_file  = "/etc/ssl/certs/cam_1/cam_1-certificate.pem.crt";
    opt.key_file  = "/etc/ssl/certs/cam_1/cam_1-private.pem.key";
    opt.verify_peer = 1;

    if (cli.newSslCtx(&opt) != 0) {
        printf("Error creando SSL context\n");
        return;
    }

    cli.setConnectTimeout(3000); 
    const char* host = "a265m6rkc34opn-ats.iot.us-east-1.amazonaws.com";
    int port = 8883;

    cli.onConnect = [](hv::MqttClient* cli) {
        printf("MQTT conectado\n");
    };

    cli.onClose = [](hv::MqttClient* cli) {
        printf("MQTT desconectado, intentando reconectar...\n");
    };

    cli.onMessage = [](hv::MqttClient* cli, mqtt_message_t* msg) {
        printf("Mensaje en topic: %.*s\n", msg->topic_len, msg->topic);
    };

    // Conexión inicial
    cli.connect(host, port, 1); // 1 = SSL

    // Hilo de reconexión
    std::thread([host, port]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            if (!cli.isConnected()) {
                printf("Reconectando MQTT...\n");
                cli.connect(host, port, 1);
            }
        }
    }).detach();

    // Hilo principal del loop de MQTT (bloqueante)
    std::thread([]() {
        cli.run();
    }).detach();
}

void sendDetectionJsonByMqtt(const hv::Json& json, std::string topic) {
    printf("Por mandar el msj\n");
    if (cli.isConnected()) {
        printf("Cliente conectado\n");
        std::string payload = json.dump();  // Convierte a string
        printf("Publicando...MQTT\n");
        cli.publish(topic, payload.c_str());
    } else {
        printf("Cliente NO conectado\n");
        syslog(LOG_WARNING, "MQTT client not connected. Skipping publish.");
    }
}

extern "C" {

#define API_STR(api, func)  "/api/" #api "/" #func
#define API_GET(api, func)  router.GET(API_STR(api, func), func)
#define API_POST(api, func) router.POST(API_STR(api, func), func)


std::mutex detector_mutex;
ma::Camera* global_camera = nullptr;
ma::Model* global_model = nullptr;
ma::engine::EngineCVI* global_engine = nullptr;
bool isInitialized = false;
int i = 1;
static HttpServer server;
std::string url = "http://192.168.4.1/report";

static void registerHttpRedirect(HttpService& router) {
    router.GET("/hotspot-detect*", [](HttpRequest* req, HttpResponse* resp) {  // IOS
        syslog(LOG_DEBUG, "[/hotspot-detect*]current url: %s -> redirect to %s\n", req->Url().c_str(), REDIRECT_URL);

        return resp->Redirect(REDIRECT_URL);
    });

    router.GET("/generate*", [](HttpRequest* req, HttpResponse* resp) {  // android
        syslog(LOG_DEBUG, "[/generate*]current url: %s -> redirect to %s\n", req->Url().c_str(), REDIRECT_URL);

        return resp->Redirect(REDIRECT_URL);
    });

    router.GET("/*.txt", [](HttpRequest* req, HttpResponse* resp) {  // windows
        syslog(LOG_DEBUG, "[/*.txt]current url: %s -> redirect to %s\n", req->Url().c_str(), REDIRECT_URL);

        return resp->Redirect(REDIRECT_URL);
    });

    router.GET("/index.html", [](HttpRequest* req, HttpResponse* resp) { return resp->File(WWW("index.html")); });
}

static void registerUserApi(HttpService& router) {
    API_POST(userMgr, login);
    API_GET(userMgr, queryUserInfo);
    // API_POST(userMgr, updateUserName); # disabled
    API_POST(userMgr, updatePassword);
    API_POST(userMgr, addSShkey);
    API_POST(userMgr, deleteSShkey);
    API_POST(userMgr, setSShStatus);
}

static void registerWiFiApi(HttpService& router) {
    API_GET(wifiMgr, queryWiFiInfo);
    API_POST(wifiMgr, scanWiFi);
    API_GET(wifiMgr, getWiFiScanResults);
    API_POST(wifiMgr, connectWiFi);
    API_POST(wifiMgr, disconnectWiFi);
    API_POST(wifiMgr, switchWiFi);
    API_GET(wifiMgr, getWifiStatus);
    API_POST(wifiMgr, autoConnectWiFi);
    API_POST(wifiMgr, forgetWiFi);
}

static void registerDeviceApi(HttpService& router) {
    API_GET(deviceMgr, getSystemStatus);
    API_GET(deviceMgr, queryServiceStatus);
    API_POST(deviceMgr, getSystemUpdateVesionInfo);
    API_GET(deviceMgr, queryDeviceInfo);
    API_POST(deviceMgr, updateDeviceName);
    API_POST(deviceMgr, updateChannel);
    API_POST(deviceMgr, setPower);
    API_POST(deviceMgr, updateSystem);
    API_GET(deviceMgr, getUpdateProgress);
    API_POST(deviceMgr, cancelUpdate);

    API_GET(deviceMgr, getDeviceList);
    API_GET(deviceMgr, getDeviceInfo);

    API_GET(deviceMgr, getAppInfo);
    API_POST(deviceMgr, uploadApp);

    API_GET(deviceMgr, getModelInfo);
    API_GET(deviceMgr, getModelFile);
    API_POST(deviceMgr, uploadModel);
    API_GET(deviceMgr, getModelList);

    API_POST(deviceMgr, savePlatformInfo);
    API_GET(deviceMgr, getPlatformInfo);
}

static void registerFileApi(HttpService& router) {
    API_GET(fileMgr, queryFileList);
    API_POST(fileMgr, uploadFile);
    API_POST(fileMgr, deleteFile);
}

static void registerLedApi(HttpService& router) {
    router.POST("/api/led/{led}/on", ledOn);
    router.POST("/api/led/{led}/off", ledOff);
    router.POST("/api/led/{led}/brightness", ledBrightness);
}

static void registerWebSocket(HttpService& router) {
    router.GET(API_STR(deviceMgr, getCameraWebsocketUrl), [](HttpRequest* req, HttpResponse* resp) {
        hv::Json data;
        data["websocketUrl"] = "ws://" + req->host + ":" + std::to_string(WS_PORT);
        hv::Json res;
        res["code"] = 0;
        res["msg"]  = "";
        res["data"] = data;

        std::string s_time = req->GetParam("time");  // req->GetString("time");
        int64_t time       = std::stoll(s_time);
        time /= 1000;  // to sec
        std::string cmd = "date -s @" + std::to_string(time);
        system(cmd.c_str());

        syslog(LOG_INFO, "WebSocket: %s\n", data["websocketUrl"]);
        return resp->Json(res);
    });
}

int initWiFi() {
    char cmd[128]        = SCRIPT_WIFI_START;
    std::string wifiName = getWiFiName("wlan0");
    
    std::thread th;

    initUserInfo();
    initSystemStatus();
    getSnCode();

    strcat(cmd, wifiName.c_str());
    strcat(cmd, " ");
    strcat(cmd, std::to_string(TTYD_PORT).c_str());
    strcat(cmd, " ");
    strcat(cmd, std::to_string(g_userId).c_str());
    system(cmd);

    char result[8] = "";
    if (0 == exec_cmd(SCRIPT_WIFI("wifi_valid"), result, NULL)) {
        if (strcmp(result, "0") == 0) {
            g_wifiMode = 4;  // No wifi module
        }
    }

    if (4 != g_wifiMode) {
        th = std::thread(monitorWifiStatusThread);
        th.detach();
    }

    th = std::thread(updateSystemThread);
    th.detach();

    return 0;
}

int stopWifi() {
    g_wifiStatus   = false;
    g_updateStatus = false;
    system(SCRIPT_WIFI_STOP);

    return 0;
}

static void initHttpsService() {
    if (0 != access(PATH_SERVER_CRT, F_OK)) {
        syslog(LOG_ERR, "The crt file does not exist\n");
        return;
    }

    if (0 != access(PATH_SERVER_KEY, F_OK)) {
        syslog(LOG_ERR, "The key file does not exist\n");
        return;
    }

    server.https_port = HTTPS_PORT;
    hssl_ctx_opt_t param;

    memset(&param, 0, sizeof(param));
    param.crt_file = PATH_SERVER_CRT;
    param.key_file = PATH_SERVER_KEY;
    param.endpoint = HSSL_SERVER;

    if (server.newSslCtx(&param) != 0) {
        syslog(LOG_ERR, "new SSL_CTX failed!\n");
        return;
    }

    syslog(LOG_INFO, "https service open successful!\n");
}

int initHttpd() {
    
    static HttpService router;
    router.AllowCORS();
    router.Static("/", WWW(""));
    router.Use(authorization);
    router.GET("/api/version", [](HttpRequest* req, HttpResponse* resp) {
        hv::Json res;
        res["code"]      = 0;
        res["msg"]       = "";
        res["data"]      = PROJECT_VERSION;
        res["uptime"]    = getUptime();
        res["timestamp"] = getTimestamp();
        return resp->Json(res);
    });


    registerHttpRedirect(router);
    registerUserApi(router);
    registerWiFiApi(router);
    registerDeviceApi(router);
    registerFileApi(router);
    registerLedApi(router);
    registerWebSocket(router);
    
    

#if HTTPS_SUPPORT
    initHttpsService();
#endif

    // server.worker_threads = 3;
    server.service = &router;
    server.port    = HTTPD_PORT;
    server.start();

    return 0;
}

int deinitHttpd() {
    server.stop();
    hv::async::cleanup();
    return 0;
}


void sendTestHttpPost(std::string payload) {
    const std::string url = "http://192.168.4.1/report";
    //const std::string payload = "E1140100004001000A";

    // Convertir URL a const char* (requerido por la librería)
    const char* url_cstr = url.c_str();

    // Configurar headers
    http_headers headers;
    headers["Content-Type"] = "text/plain";

    // Enviar petición POST
    MA_LOGI("LOG_INFO", "Enviando prueba HTTP POST a %s", url_cstr);
    MA_LOGI("LOG_DEBUG", "Payload: %s", payload.c_str());
    
    auto resp = requests::post(url_cstr, payload, headers);

    // Manejar respuesta
    if (resp == nullptr) {
        MA_LOGI("LOG_ERR", "Error: No se recibió respuesta del servidor");
    } else {
        MA_LOGI("LOG_INFO", "Respuesta HTTP %d - Contenido: %s", 
              resp->status_code, resp->body.c_str());
    }
}

static bool initialize_resources() {
    global_camera = initialize_camera();
    if (!global_camera) {
        MA_LOGE(TAG, "Camera initialization failed");
        return false;
    }

    global_model = initialize_model("yolo11n_helmetPerson_int8_sym.cvimodel");
    if (!global_model) {
        MA_LOGE(TAG, "Model initialization failed");
        release_camera(global_camera);
        return false;
    }

    return true;
}

static void clean_resources() {
    if (global_camera) {
        release_camera(global_camera);
        global_camera = nullptr;
    }
    if (global_model) {
        release_model(global_model, global_engine);
        global_model = nullptr;
    }

}

static void camera_failure() {
    std::lock_guard<std::mutex> lock(detector_mutex);
    release_camera(global_camera);
    global_camera = initialize_camera();
    if (!global_camera) {
        MA_LOGE(TAG, "Critical camera failure");
        clean_resources();
        exit(1);
    }
}

struct AlarmConfig {
    std::string json_key; 
    std::string payload; 
    std::string topic;     
    std::chrono::steady_clock::time_point* last_alert; 
};

void process_detection_results(nlohmann_json& parsed,uint8_t* frame, std::chrono::steady_clock::time_point& last_helmet_alert,std::chrono::steady_clock::time_point& last_zone_alert, std::chrono::steady_clock::time_point& last_person_report) {
    auto now = std::chrono::steady_clock::now();

    if (parsed.contains("image_saved")) {
        i++;
        MA_LOGD(TAG, "Imagen guardada, contador incrementado a %d", i);
    }

    try {
        std::vector<AlarmConfig> alarms = {
            {"detected_no_helmet", "E1140140002103", "industria4-0/devices/cam_1/events", &last_helmet_alert},
            {"restricted_zone_violation", "E1140140002102", "industria4-0/devices/cam_1/events", &last_zone_alert}
        };
        for (const auto& alarm : alarms) {
            if (parsed.contains(alarm.json_key)) {
                if (alarm.last_alert) *(alarm.last_alert) = now;

                nlohmann_json alarm_msg;
                alarm_msg["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
                alarm_msg["payload"]   = alarm.payload;

                std::string msg_str = alarm_msg.dump();
                MA_LOGD(TAG, "Enviando alerta %s: %s", alarm.json_key.c_str(), msg_str.c_str());

                if (connectivity_mode == ConnectivityMode::MQTT) {
                    std::cout << "Mandando alarma por MQTT : " << std::endl;
                    sendDetectionJsonByMqtt(msg_str.c_str(), alarm.topic.c_str());
                } else if (connectivity_mode == ConnectivityMode::HTTP) {
                    sendTestHttpPost(alarm.payload);
                }
            }
        }

        if (parsed.contains("person_count")) {
            last_person_report = now;
            int person_count = parsed["person_count"].get<int>();

            nlohmann_json report_msg;
            report_msg["count"] = person_count;
            report_msg["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();

            std::stringstream payload;
            payload << "E1140100004001"
                    << std::setw(4) << std::setfill('0') << std::hex << person_count;
            report_msg["payload"] = payload.str();

            std::string msg_str = report_msg.dump();
            MA_LOGD(TAG, "Enviando reporte de personas: %s", msg_str.c_str());

            if (connectivity_mode == ConnectivityMode::MQTT) {
                sendDetectionJsonByMqtt(msg_str.c_str(), "industria4-0/devices/cam_1/status");
            } else if (connectivity_mode == ConnectivityMode::HTTP) {
                sendTestHttpPost(report_msg["payload"].get<std::string>());
            }
        }
    }
    catch (const nlohmann_json::exception& e) {
        MA_LOGE(TAG, "Error procesando JSON: %s", e.what());
    }
    catch (const std::exception& e) {
        MA_LOGE(TAG, "Error inesperado: %s", e.what());
    }
}


int connectToReCamNet() {
    MA_LOGI("WiFi", "Initializing ReCamNet connection...");
    
    // 1. Ensure interface is up
    system("ifconfig wlan0 up");
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 2. Kill existing WiFi processes (optional cleanup)
    system("killall wpa_supplicant > /dev/null 2>&1");
    system("killall dhclient > /dev/null 2>&1");

    // 3. Start wpa_supplicant
    MA_LOGI("WiFi", "Starting WiFi services...");
    system("wpa_supplicant -B -i wlan0 -c /etc/wpa_supplicant.conf -D nl80211,wext");

    // 4. Add ReCamNet network without removing existing ones
    char add_net_result[64] = "";
    exec_cmd("wpa_cli -i wlan0 add_network", add_net_result, NULL);

    int net_id = atoi(add_net_result);  // network ID asignado automáticamente
    if (net_id < 0) {
        MA_LOGE("WiFi", "Failed to add new network");
        return -1;
    }

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "wpa_cli -i wlan0 set_network %d ssid '\"ReCamNet\"'", net_id);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "wpa_cli -i wlan0 set_network %d psk '\"12345678\"'", net_id);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "wpa_cli -i wlan0 set_network %d key_mgmt WPA-PSK", net_id);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "wpa_cli -i wlan0 enable_network %d", net_id);
    system(cmd);
    system("wpa_cli -i wlan0 save_config");
    system("wpa_cli -i wlan0 reassociate");

    // 5. Verify connection
    MA_LOGI("WiFi", "Verifying association...");
    for(int i = 0; i < 15; i++) {
        char result[256] = "";
        exec_cmd("iwconfig wlan0 | grep 'ESSID:\"ReCamNet\"'", result, NULL);
        
        if(strstr(result, "ReCamNet")) {
            MA_LOGI("WiFi", "✓ Associated with ReCamNet");

            // Optional: check IP assignment
            exec_cmd("ifconfig wlan0 | grep 'inet addr'", result, NULL);
            if(strlen(result) > 0) {
                MA_LOGI("WiFi", "IP Address: %s", strtok(result, " "));
            } else {
                MA_LOGW("WiFi", "No IP assigned (expected for LoRaWAN bridge)");
            }
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // 6. Failure diagnosis
    MA_LOGE("WiFi", "Association failed");
    char diag[512];
    exec_cmd("dmesg | grep wlan0 | tail -n 15", diag, NULL);
    MA_LOGD("WiFi", "Kernel logs:\n%s", diag);

    exec_cmd("wpa_cli -i wlan0 status", diag, NULL);
    MA_LOGD("WiFi", "wpa_supplicant status:\n%s", diag);

    return -1;
}

void initConnectivity() {

    if (connectivity_mode == ConnectivityMode::MQTT){
        std::thread th;
        MA_LOGI("System", "Iniciando modo MQTT...");
        th           = std::thread(initMqtt);
        th.detach();
    } 
    if (connectivity_mode == ConnectivityMode::HTTP){
        MA_LOGI("System", "Iniciando modo WiFi...");
        int wifiStatus = connectToReCamNet();
        
        if (wifiStatus == 0) {
            MA_LOGI("System", "Sistema WiFi listo");
        } else {
            MA_LOGE("System", "Fallo crítico en conexión WiFi");
        }
    } 
}



}  // extern "C" {