#include <thread>       // 解决std::this_thread错误
#include <mutex>
#include <memory>
#include <sscma.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <syslog.h>
#include <thread>
#include <model_detector.h>
#include "hv/HttpServer.h"
#include "hv/hasync.h"   // import hv::async
#include "hv/hthread.h"  // import hv_gettid
#include "common.h"
#include "http.h"
#include "utils_device.h"
#include "utils_file.h"
#include "utils_led.h"
#include "utils_user.h"
#include "utils_wifi.h"
#include "version.h"
#include "frame_builder.h"
#include "mqtt_client.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <sstream>
#include <iomanip>

std::string bytes_to_hex(const uint8_t* bytes, size_t length) {
    std::ostringstream oss;
    for (size_t i = 0; i < length; ++i)
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)bytes[i];
    return oss.str();
}

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


extern "C" {

#define API_STR(api, func)  "/api/" #api "/" #func
#define API_GET(api, func)  router.GET(API_STR(api, func), func)
#define API_POST(api, func) router.POST(API_STR(api, func), func)

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

MqttContext* configurar_mqtt_aws() {
    // Configuración de certificados (ajusta las rutas según tu entorno)
    const char* ca_cert = "/etc/ssl/certs/cam_1/AmazonRootCA1.pem";
    const char* cert = "/etc/ssl/certs/cam_1/cam_1-certificate.pem.crt";
    const char* key = "/etc/ssl/certs/cam_1/cam_1-private.pem.key";
    const char* aws_endpoint = "a265m6rkc34opn-ats.iot.us-east-1.amazonaws.com";
    const char* cli_id = "cam_1";

    // Crear contexto MQTT
    MqttContext* ctx = mqtt_context_create();
    if (!ctx) {
        MA_LOGE(TAG, "Error al crear contexto MQTT");
        return NULL;
    }

    // Configurar TLS para AWS IoT Core (autenticación mutua requerida)
    if (mqtt_configure_tls(ctx, ca_cert, cert, key, 1) != 0) {
        MA_LOGE(TAG, "Error al configurar TLS para AWS IoT");
        mqtt_context_free(ctx);
        return NULL;
    }

    // Conectar al endpoint de AWS
    printf("Conectando a AWS IoT Core en %s con client_id %s...\n", aws_endpoint, cli_id);
    if (mqtt_connect(ctx, aws_endpoint, 8883, cli_id, 1, 5000) != 0) {
        MA_LOGE(TAG, "Error al conectar con AWS IoT Core");
        mqtt_context_free(ctx);
        return NULL;
    }
    printf("Conexión a AWS IoT Core exitosa\n");
    return ctx;
}


int publicar_mensaje_aws(MqttContext* ctx, const char* topic, const std::string& payload, int qos) {
    if (!ctx) return -1;

    // Publicar mensaje con QoS 1 (asegura entrega al menos una vez)
    int result = mqtt_publish(ctx, topic, payload.c_str(), 0, 3000);
    
    if (result < 0) {
        MA_LOGE(TAG, "Error al publicar mensaje en topic %s", topic);
        return -1;
    }
    
    return 0;
}
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
// Variable global para el contexto MQTT
MqttContext* global_mqtt_ctx = nullptr;
std::mutex detector_mutex;
ma::Camera* global_camera = nullptr;
ma::Model* global_model = nullptr;
ma::engine::EngineCVI* global_engine = nullptr;
bool isInitialized = false;
int i = 1;
// 1. Declaraciones adelantadas
static bool initialize_resources();
static void cleanup_resources();
static void handle_camera_failure();
static void process_detection_results(json& parsed, uint8_t* frame, 
                                    std::chrono::steady_clock::time_point& last_helmet_alert,
                                    std::chrono::steady_clock::time_point& last_person_report);
static void safe_mqtt_publish(const char* topic, const std::string& payload, int qos);
static int mqtt_reconnect();

// 2. Implementación de funciones auxiliares
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
    if (global_mqtt_ctx) {
        mqtt_context_free(global_mqtt_ctx);
        global_mqtt_ctx = nullptr;
    }
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
                                    std::chrono::steady_clock::time_point& last_person_report) {
    auto now = std::chrono::steady_clock::now();

    if (parsed.contains("image_saved")) {
        i++;
    }

    if (parsed.contains("detected_no_helmet")) {
        last_helmet_alert = now;
        
        // Crear mensaje JSON estructurado para la alarma
        json alarm_msg;
        alarm_msg["device_id"] = "cam_1";
        alarm_msg["alarm_type"] = "no_helmet";
        alarm_msg["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
        alarm_msg["image_path"] = parsed["image_saved"];
        
        safe_mqtt_publish("industria4-0/devices/cam_1/events", alarm_msg.dump(), 1);
    }

    if (parsed.contains("person_count")) {
        last_person_report = now;
        
        // Crear mensaje JSON para el reporte de personas
        json report_msg;
        report_msg["device_id"] = "cam_1";
        report_msg["count"] = parsed["person_count"];
        report_msg["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
        
        safe_mqtt_publish("industria4-0/devices/cam_1/status", report_msg.dump(), 1);
    }
}

static void safe_mqtt_publish(const char* topic, const std::string& payload, int qos) {
    try {
        if (!global_mqtt_ctx) {
            MA_LOGE(TAG, "MQTT context not available");
            return;
        }

        printf("Publicando en %s: %s\n", topic, payload.c_str());
        int result = publicar_mensaje_aws(global_mqtt_ctx, topic, payload, qos);
        
        if (result != 0) {
            MA_LOGE(TAG, "Error al publicar en %s (código %d)", topic, result);
            if (mqtt_reconnect() != 0) {
                MA_LOGE(TAG, "No se pudo reconectar MQTT");
            }
        }
    } catch (const std::exception& e) {
        MA_LOGE(TAG, "Exception in MQTT publish: %s", e.what());
    }
}

static int mqtt_reconnect() {
    if (!global_mqtt_ctx) return -1;
    
    mqtt_context_free(global_mqtt_ctx);
    global_mqtt_ctx = configurar_mqtt_aws();
    
    if (!global_mqtt_ctx) {
        MA_LOGE(TAG, "Failed to reconnect MQTT");
        return -1;
    }
    
    std::thread([] {
        hloop_run(mqtt_context_get_loop(global_mqtt_ctx));
    }).detach();
    
    return 0;
}

// 3. Función principal del detector
static void handle_detection_cycle() {
    uint8_t frame[9];
    auto last_helmet_alert = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    auto last_person_report = std::chrono::steady_clock::now();

    while (true) {
        try {
            std::lock_guard<std::mutex> lock(detector_mutex);
            
            auto now = std::chrono::steady_clock::now();
            bool report_person = (now - last_person_report) >= std::chrono::seconds(5);
            bool can_alert = (now - last_helmet_alert) >= std::chrono::seconds(10);

            std::string result = model_detector(global_model, global_camera, i, report_person, can_alert);
            json parsed = json::parse(result);

            if (parsed.contains("error")) {
                MA_LOGE(TAG, "Detector error: %s", parsed["error"].get<std::string>().c_str());
                continue;
            }

            if (result.find("Camera capture failed") != std::string::npos) {
                handle_camera_failure();
                continue;
            }

            process_detection_results(parsed, frame, last_helmet_alert, last_person_report);

            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        } catch (const std::exception& e) {
            MA_LOGE(TAG, "Exception in detection cycle: %s", e.what());
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

// 4. Función de registro principal
static void registermodeldetector(HttpService& router) {
    // Inicializar MQTT
    global_mqtt_ctx = configurar_mqtt_aws();
    if (!global_mqtt_ctx) {
        MA_LOGE(TAG, "No se pudo inicializar MQTT");
        return;
    }

    // Iniciar loop MQTT
    std::thread([] {
        hloop_run(mqtt_context_get_loop(global_mqtt_ctx));
    }).detach();

    // Inicializar recursos
    if (!isInitialized && !initialize_resources()) {
        cleanup_resources();
        return;
    }
    isInitialized = true;

    // Iniciar hilo de detección
    std::thread(handle_detection_cycle).detach();
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


static int response_status(HttpResponse* resp, int code = 200, const char* message = NULL) {
    if (message == NULL)
        message = http_status_str((enum http_status)code);
    resp->Set("code", code);
    resp->Set("message", message);
    return code;
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
    registermodeldetector(router);


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
