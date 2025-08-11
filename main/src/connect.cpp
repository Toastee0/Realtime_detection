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
using json = nlohmann::json;
using namespace hv;

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

ConnectivityMode connectivity_mode = ConnectivityMode::MQTT;

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

std::atomic<bool> keepRunning{true};
std::string current_ssid;
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
/*
void on_wifi_connected(const char* ssid) {
    current_ssid = ssid;
}

// Y la función:

bool is_connected_to_esp_ap() {
    return (current_ssid == "ReCamNet");
}

bool check_internet_connection() {
    MA_LOGI(TAG, "Verificando conexión a Internet...");
    char result[CMD_BUF_SIZE];
    int ret = exec_cmd("ping -c 1 -W 2 8.8.8.8", result, NULL);
    
    if (ret == 0) {
        MA_LOGI(TAG, "Conexión a Internet disponible");
        return true;
    }
    
    MA_LOGE(TAG, "No se detectó conexión a Internet. Código de error: %d", ret);
    return false;
}

bool ensure_wifi_connection() {
    if (g_wifiStatus) {
        MA_LOGI(TAG, "WiFi ya está conectado");
        return true;
    }

    MA_LOGI(TAG, "Intentando conectar WiFi...");
    HttpRequest dummy_req;
    HttpResponse dummy_resp;
    
    for (int i = 0; i < 3; i++) {  // 3 intentos
        if (autoConnectWiFi(&dummy_req, &dummy_resp) == 0) {
            MA_LOGI(TAG, "Conexión WiFi exitosa (intento %d/3)", i+1);
            return true;
        }
        MA_LOGW(TAG, "Fallo conexión WiFi (intento %d/3)", i+1);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    MA_LOGE(TAG, "No se pudo conectar al WiFi después de 3 intentos");
    return false;
}

int send_http_post(const std::string& payload, const std::string& url) {
    MA_LOGI(TAG, "Preparando envío HTTP a %s", url.c_str());
    
    try {
        auto resp = requests::post(url.c_str(), payload);
        
        if (!resp) {
            MA_LOGE(TAG, "Error: No hubo respuesta del servidor HTTP");
            return 0;
        }

        MA_LOGI(TAG, "HTTP código de respuesta: %d", resp->status_code);
        
        if (resp->status_code >= 400) {
            MA_LOGE(TAG, "Error HTTP: %d", resp->status_code);
            return 0;
        }
        
        return 1;
    } catch (const std::exception& e) {
        MA_LOGE(TAG, "Excepción en HTTP POST: %s", e.what());
        return 0;
    }
}
*/

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

static void process_detection_results(json& parsed, uint8_t* frame,std::chrono::steady_clock::time_point& last_helmet_alert, std::chrono::steady_clock::time_point& last_zone_alert,std::chrono::steady_clock::time_point& last_person_report) {
    auto now = std::chrono::steady_clock::now();
    
    if (parsed.contains("image_saved")) {
        i++;
        MA_LOGD(TAG, "Imagen guardada, contador incrementado a %d", i);
    }
    
    try {
        if (parsed.contains("detected_no_helmet")) {
            last_helmet_alert = now;
            json alarm_msg;
            alarm_msg["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
            alarm_msg["payload"] = "E1140140002103";

            std::string msg_str = alarm_msg.dump();
            MA_LOGD(TAG, "Enviando alerta de casco: %s", msg_str.c_str());
            
            if (connectivity_mode == ConnectivityMode::MQTT) {
                sendDetectionJsonByMqtt(msg_str.c_str(), "industria4-0/devices/cam_1/events"); 
            }else if (connectivity_mode == ConnectivityMode::HTTP) {
                sendTestHttpPost(alarm_msg["payload"].get<std::string>());   
            } 
        }

        if (parsed.contains("restricted_zone_violation")) {
            last_zone_alert = now;
            json alarm_msg;
            alarm_msg["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
            alarm_msg["payload"] = "E1140140002102";

            std::string msg_str = alarm_msg.dump();
            MA_LOGD(TAG, "Enviando alerta de zona restringida: %s", msg_str.c_str());
            
            if (connectivity_mode == ConnectivityMode::MQTT) {
                sendDetectionJsonByMqtt(msg_str.c_str(), "industria4-0/devices/cam_1/events"); 
            }else if (connectivity_mode == ConnectivityMode::HTTP) {
                sendTestHttpPost(alarm_msg["payload"].get<std::string>());   
            } 
        }

        if (parsed.contains("person_count")) {
            last_person_report = now;
            json report_msg;
            report_msg["count"] = parsed["person_count"];
            report_msg["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
            int person_count = parsed["person_count"].get<int>();
            std::stringstream payload;
            payload << "E1140100004001" << std::setw(4) << std::setfill('0') << std::hex << person_count;
            report_msg["payload"] = payload.str();

            std::string msg_str = report_msg.dump();
            MA_LOGD(TAG, "Enviando reporte de personas: %s", msg_str.c_str());
            
            if (connectivity_mode == ConnectivityMode::MQTT) {
                sendDetectionJsonByMqtt(msg_str.c_str(), "industria4-0/devices/cam_1/status"); 
            } else if (connectivity_mode == ConnectivityMode::HTTP) {
                sendTestHttpPost(report_msg["payload"].get<std::string>());   
            } 
        }
    } catch (const json::exception& e) {
        MA_LOGE(TAG, "Error procesando JSON: %s", e.what());
    } catch (const std::exception& e) {
        MA_LOGE(TAG, "Error inesperado: %s", e.what());
    }
}

static void register_model_detector() {
    if (!isInitialized && !initialize_resources()) {
        MA_LOGE(TAG, "Resource initialization failed");
        clean_resources();
        return;
    }
    isInitialized = true;

    std::thread([] {
        MA_LOGI(TAG, "Starting detection cycle");

        uint8_t frame[9];
        auto last_helmet_alert = std::chrono::steady_clock::now() - std::chrono::seconds(10);
        auto last_zone_alert = std::chrono::steady_clock::now() - std::chrono::seconds(10);
        auto last_person_report = std::chrono::steady_clock::now();
        auto last_processing_time = std::chrono::steady_clock::now();

        while (keepRunning) {
            try {
                auto processing_start = std::chrono::steady_clock::now();
                
                {
                    static int frame_id = 0;
                    frame_id++;
                    printf("[Detector] Frame #%d processed\n", frame_id);
                    
                    std::lock_guard<std::mutex> lock(detector_mutex);
                    auto now = std::chrono::steady_clock::now();
                    
                    bool report_person = (now - last_person_report) >= std::chrono::seconds(5);
                    bool can_alert = (now - last_helmet_alert) >= std::chrono::seconds(10);
                    bool can_zone = (now - last_zone_alert) >= std::chrono::seconds(10);
                    
                    std::string result = model_detector(global_model, global_camera, i, report_person, can_alert, can_zone);
                    
                    json parsed = json::parse(result);
                    if (parsed.contains("error")) {
                        MA_LOGE(TAG, "Detector error: %s", parsed["error"].get<std::string>().c_str());
                        continue;
                    }
                    if (result.find("Camera capture failed") != std::string::npos) {
                        camera_failure();
                        continue;
                    }
                    
                    process_detection_results(parsed, frame, last_helmet_alert, last_zone_alert, last_person_report);
                }

                auto processing_end = std::chrono::steady_clock::now();
                auto processing_duration = std::chrono::duration_cast<std::chrono::milliseconds>(processing_end - processing_start);
                
                std::this_thread::sleep_for(std::chrono::milliseconds(5000));
                
            } catch (const std::exception& e) {
                MA_LOGE(TAG, "Detection cycle exception: %s", e.what());
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }).detach();
}

int initWiFi() {
    char cmd[128]        = SCRIPT_WIFI_START;
    std::string wifiName = getWiFiName("wlan0");
    MA_LOGI("System", "Iniciando modo MQTT... %s", wifiName.c_str());
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

int connectToReCamNet() {
    MA_LOGI("WiFi", "Initializing ReCamNet connection...");
    
    // 1. Verify interface state
    system("ifconfig wlan0 up"); // Ensure interface is up
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 2. Kill existing WiFi processes
    system("killall wpa_supplicant > /dev/null 2>&1");
    system("killall dhclient > /dev/null 2>&1");

    // 3. Start clean wpa_supplicant
    MA_LOGI("WiFi", "Starting WiFi services...");
    system("wpa_supplicant -B -i wlan0 -c /etc/wpa_supplicant.conf -D nl80211,wext");

    // 4. Manual network configuration
    const char* setup_commands[] = {
        "wpa_cli -i wlan0 remove_network all",
        "wpa_cli -i wlan0 add_network",
        "wpa_cli -i wlan0 set_network 0 ssid '\"ReCamNet\"'",
        "wpa_cli -i wlan0 set_network 0 psk '\"12345678\"'",
        "wpa_cli -i wlan0 set_network 0 key_mgmt WPA-PSK",
        "wpa_cli -i wlan0 enable_network 0",
        "wpa_cli -i wlan0 save_config",
        "wpa_cli -i wlan0 reassociate",
        NULL
    };

    for(int i = 0; setup_commands[i]; i++) {
        if(system(setup_commands[i]) != 0) {
            MA_LOGE("WiFi", "Command failed: %s", setup_commands[i]);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // 5. Verify connection
    MA_LOGI("WiFi", "Verifying association...");
    for(int i = 0; i < 15; i++) { // 15-second timeout
        char result[256] = "";
        exec_cmd("iwconfig wlan0 | grep 'ESSID:\"ReCamNet\"'", result, NULL);
        
        if(strstr(result, "ReCamNet")) {
            MA_LOGI("WiFi", "✓ Associated with ReCamNet");
            
            // Check for IP assignment (optional for local network)
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

int connectToOriginalNet() {
    stopWifi(); // mata procesos actuales

    // Ejecutar el script original para volver a conectarse a la red inicial
    return system(SCRIPT_WIFI_START);
}

void initConnectivity() {
/*
    // Estrategia de conectividad
    if (check_internet_connection()) {
        connectivity_mode = ConnectivityMode::MQTT;
    } 
    else if (is_connected_to_esp_ap()) {
        connectivity_mode = ConnectivityMode::HTTP;
        MA_LOGI(TAG, "Modo HTTP activado (WiFi local)");
    }
    else {
        connectivity_mode = ConnectivityMode::HTTP;
        MA_LOGW(TAG, "Modo HTTP activado (sin conexión)");
    }
*/


    if (connectivity_mode == ConnectivityMode::MQTT){
        MA_LOGI("System", "Iniciando modo MQTT...");
        connectToOriginalNet();
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
    register_model_detector();
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

}  // extern "C" {