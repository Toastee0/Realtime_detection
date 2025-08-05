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
#include "utils_device.h"
#include "utils_file.h"
#include "utils_led.h"
#include "utils_user.h"
#include "utils_wifi.h"
#include "version.h"
#include "frame_builder.h"
#include "hv/mqtt_client.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>

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

ConnectivityMode connectivity_mode = ConnectivityMode::NONE;

extern "C" {

#define API_STR(api, func)  "/api/" #api "/" #func
#define API_GET(api, func)  router.GET(API_STR(api, func), func)
#define API_POST(api, func) router.POST(API_STR(api, func), func)


class MQTTManager {
private:
    std::mutex mtx_;
    hv::MqttClient* client_ = nullptr;
    std::thread loop_thread_;
    std::atomic<bool> connecting_{false};
    std::atomic<bool> running_{false};
    
    // Configuración
    const std::string host_ = "a265m6rkc34opn-ats.iot.us-east-1.amazonaws.com";
    const int port_ = 8883;
    const int ssl_ = 1;
    const std::string client_id_ = "cam_1";
    
    // SSL config
    const std::string ca_file_ = "/etc/ssl/certs/cam_1/AmazonRootCA1.pem";
    const std::string crt_file_ = "/etc/ssl/certs/cam_1/cam_1-certificate.pem.crt";
    const std::string key_file_ = "/etc/ssl/certs/cam_1/cam_1-private.pem.key";
    
public:
    MQTTManager() = default;
    ~MQTTManager() {
        disconnect();
    }
    
    bool is_connected() const {
        return client_ && client_->isConnected();
    }
    
    bool connect() {
        if (connecting_.exchange(true)) {
            MA_LOGW(TAG, "Already attempting to connect");
            return false;
        }
        
        std::lock_guard<std::mutex> lock(mtx_);
        
        try {
            // Limpiar conexión existente
            if (client_) {
                client_->disconnect();
                if (loop_thread_.joinable()) {
                    running_ = false;
                    loop_thread_.join();
                }
                delete client_;
                client_ = nullptr;
            }
            
            // Configurar nuevo cliente
            client_ = new hv::MqttClient();
            client_->setID(client_id_.c_str());
            
            // Configurar SSL
            hssl_ctx_opt_t ssl_opt;
            memset(&ssl_opt, 0, sizeof(ssl_opt));
            ssl_opt.ca_file = ca_file_.c_str();
            ssl_opt.crt_file = crt_file_.c_str();
            ssl_opt.key_file = key_file_.c_str();
            ssl_opt.verify_peer = 1;
            
            if (client_->newSslCtx(&ssl_opt) != 0) {
                MA_LOGE(TAG, "Failed to configure SSL");
                delete client_;
                client_ = nullptr;
                connecting_ = false;
                return false;
            }
            
            // Configurar callbacks
            client_->onConnect = [this](hv::MqttClient* cli) {
                MA_LOGI(TAG, "MQTT connected");
                connecting_ = false;
            };
            
            client_->onClose = [this](hv::MqttClient* cli) {
                MA_LOGW(TAG, "MQTT disconnected");
                if (running_) {
                    MA_LOGI(TAG, "Attempting to reconnect...");
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                    connect();
                }
            };
            
            // Conectar
            int ret = client_->connect(host_.c_str(), port_, ssl_);
            if (ret != 0) {
                MA_LOGE(TAG, "MQTT connect failed: %d", ret);
                delete client_;
                client_ = nullptr;
                connecting_ = false;
                return false;
            }
            
            // Iniciar loop en hilo separado
            running_ = true;
            loop_thread_ = std::thread([this]() {
                while (running_) {
                    if (client_) {
                        client_->run();
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            });
            
            return true;
        } catch (const std::exception& e) {
            MA_LOGE(TAG, "MQTT connect exception: %s", e.what());
            connecting_ = false;
            return false;
        }
    }
    
    void disconnect() {
        running_ = false;
        connecting_ = false;
        
        std::lock_guard<std::mutex> lock(mtx_);
        
        if (client_) {
            client_->disconnect();
        }
        
        if (loop_thread_.joinable()) {
            loop_thread_.join();
        }
        
        if (client_) {
            delete client_;
            client_ = nullptr;
        }
    }
    
    bool publish(const std::string& topic, const std::string& payload, int qos) {
        std::lock_guard<std::mutex> lock(mtx_);
        
        if (!client_ || !client_->isConnected()) {
            MA_LOGW(TAG, "MQTT not connected for publish");
            return false;
        }
        
        try {
            int result = client_->publish(topic.c_str(), payload, qos);
            if (result < 0) {
                MA_LOGE(TAG, "MQTT publish failed: %d", result);
                return false;
            }
            return true;
        } catch (const std::exception& e) {
            MA_LOGE(TAG, "MQTT publish exception: %s", e.what());
            return false;
        }
    }
};

// Reemplazar global_mqtt_client con:



// Variable global para el contexto MQTT
//MqttContext* global_mqtt_ctx = nullptr;
std::mutex detector_mutex;
ma::Camera* global_camera = nullptr;
ma::Model* global_model = nullptr;
ma::engine::EngineCVI* global_engine = nullptr;
std::thread mqtt_loop_thread;
bool isInitialized = false;
int i = 1;
MQTTManager mqtt_manager;
static HttpServer server;


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



 // Función para publicar con reintentos
int safe_mqtt_publish(const char* topic, const std::string& payload, int qos, int max_retries = 2) {
    for (int attempt = 0; attempt < max_retries; ++attempt) {
        if (mqtt_manager.publish(topic, payload, qos)) {
            return 1;
        }
        
        if (attempt < max_retries - 1) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            // Intentar reconectar solo una vez
            if (attempt == 0 && !mqtt_manager.is_connected()) {
                MA_LOGI(TAG, "Attempting to reconnect...");
                mqtt_manager.connect();
            }
        }
    }
    
    return 0;
}

// Resto del código con las adaptaciones necesarias
std::string frame_to_hex_string(const uint8_t* frame, size_t len) {
    static const char* hex_digits = "0123456789ABCDEF";
    std::string hex;
    hex.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        hex += hex_digits[(frame[i] >> 4) & 0x0F];
        hex += hex_digits[frame[i] & 0x0F];
    }
    return hex;
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

static void cleanup_resources() {
    if (global_camera) {
        release_camera(global_camera);
        global_camera = nullptr;
    }
    if (global_model) {
        release_model(global_model, global_engine);
        global_model = nullptr;
    }
    mqtt_manager.disconnect();
}

static void handle_camera_failure() {
    std::lock_guard<std::mutex> lock(detector_mutex);
    release_camera(global_camera);
    global_camera = initialize_camera();
    if (!global_camera) {
        MA_LOGE(TAG, "Critical camera failure");
        cleanup_resources();
        exit(1);
    }
}

static void process_detection_results(json& parsed, uint8_t* frame, 
                                    std::chrono::steady_clock::time_point& last_helmet_alert, 
                                    std::chrono::steady_clock::time_point& last_zone_alert,
                                    std::chrono::steady_clock::time_point& last_person_report) {
    auto now = std::chrono::steady_clock::now();
    
    // Debug: Verificar estado de conexión
    MA_LOGI(TAG, "Estado MQTT antes de publicar: conectado=%d", mqtt_manager.is_connected());
    
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
            
            if (!safe_mqtt_publish("industria4-0/devices/cam_1/events", msg_str, 1)) {
                MA_LOGE(TAG, "Fallo al publicar alerta de casco");
            }
        }

        if (parsed.contains("restricted_zone_violation")) {
            last_zone_alert = now;
            json alarm_msg;
            alarm_msg["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
            alarm_msg["payload"] = "E1140140002102";

            std::string msg_str = alarm_msg.dump();
            MA_LOGD(TAG, "Enviando alerta de zona restringida: %s", msg_str.c_str());
            
            if (!safe_mqtt_publish("industria4-0/devices/cam_1/events", msg_str, 1)) {
                MA_LOGE(TAG, "Fallo al publicar alerta de zona restringida");
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
            
            if (!safe_mqtt_publish("industria4-0/devices/cam_1/status", msg_str, 1)) {
                MA_LOGE(TAG, "Fallo al publicar reporte de personas");
            }
        }
    } catch (const json::exception& e) {
        MA_LOGE(TAG, "Error procesando JSON: %s", e.what());
    } catch (const std::exception& e) {
        MA_LOGE(TAG, "Error inesperado: %s", e.what());
    }
}

static void handle_detection_cycle() {
    uint8_t frame[9];
    auto last_helmet_alert = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    auto last_zone_alert = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    auto last_person_report = std::chrono::steady_clock::now();

    while (true) {
        try {
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
                handle_camera_failure();
                continue;
            }

            process_detection_results(parsed, frame, last_helmet_alert, last_zone_alert, last_person_report);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        } catch (const std::exception& e) {
            MA_LOGE(TAG, "Detection cycle exception: %s", e.what());
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}

static void register_model_detector() {
    MA_LOGI(TAG, "Starting register_model_detector");

    if (connectivity_mode == ConnectivityMode::MQTT && !mqtt_manager.is_connected()) {
        MA_LOGE(TAG, "MQTT not initialized despite connectivity mode");
        return;
    }

    MA_LOGI(TAG, "Checking resource initialization...");
    if (!isInitialized && !initialize_resources()) {
        cleanup_resources();
        return;
    }
    MA_LOGI(TAG, "Resources initialized successfully");
    isInitialized = true;

    std::thread([] {
        try {
            MA_LOGI(TAG, "Starting detection cycle");
            handle_detection_cycle();
            MA_LOGI(TAG, "Detection cycle exited normally");
        } catch (const std::exception& ex) {
            MA_LOGE(TAG, "Exception in detection cycle: %s", ex.what());
        } catch (...) {
            MA_LOGE(TAG, "Unknown exception in detection cycle");
        }
    }).detach();
}

int check_internet_connection() {
    int result = system("ping -c 1 -W 2 8.8.8.8 > /dev/null 2>&1");
    return (result == 0);
}

int connect_to_wifi(const std::string& ssid, const std::string& password) {
    std::string command = "nmcli device wifi connect '" + ssid + "' password '" + password + "'";
    int result = system(command.c_str());
    return (result == 0);
}

int send_http_post(const std::string& payload, const std::string& url) {
    syslog(LOG_DEBUG, "[send_http_post] POST to %s, payload len=%zu",
           url.c_str(), payload.size());

    auto resp = requests::post(url.c_str(), payload);
    if (!resp) {
        syslog(LOG_ERR, "[send_http_post] request failed (resp == NULL)");
        return 0;
    }

    int code = resp->status_code;
    syslog(LOG_DEBUG, "[send_http_post] HTTP code=%d", code);

    if (code >= 400) {
        std::cerr << "HTTP POST failed: code=" << code << "\n";
        syslog(LOG_ERR, "[send_http_post] error code=%d", code);
        return 0;
    }
    return 1;
}

void send_event_message(const char* topic, const std::string& payload, int qos) {
    switch (connectivity_mode) {
        case ConnectivityMode::MQTT:
            safe_mqtt_publish(topic, payload, qos);
            break;
        case ConnectivityMode::HTTP_TO_ESP:
            send_http_post(payload, topic); // Note: parámetros invertidos para coincidir con tu implementación
            break;
        default:
            MA_LOGE(TAG, "Connectivity mode not set");
            break;
    }
}

void initConnectivity() {
    if (check_internet_connection()) {
        MA_LOGI(TAG, "Internet available. Using MQTT.");
        connectivity_mode = ConnectivityMode::MQTT;
        
        if (!mqtt_manager.connect()) {
            MA_LOGE(TAG, "MQTT initialization failed. Switching to HTTP.");
            connectivity_mode = ConnectivityMode::HTTP_TO_ESP;
        }
    } else {
        MA_LOGW(TAG, "No internet. Using HTTP fallback.");
        connectivity_mode = ConnectivityMode::HTTP_TO_ESP;
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
/*
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
    registerWebSocket(router);*/
    
    

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

}  // extern "C" {