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
#include "global_cfg.h"
#include "config.h"
#include <cstdio>
#include <ctime>
using nlohmann_json = nlohmann::json;
using namespace hv;

hv::MqttClient cli;
auto internal_mode = 0; //0: mqtt - 1:http

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

    std::string pth = "/etc/ssl/certs/";
    std::string disp = g_cfg.disp;

    // Crear strings completas primero
    std::string ca_file_str = pth + disp + "/AmazonRootCA1.pem";
    std::string crt_file_str = pth + disp + "/" + disp + "-certificate.pem.crt";
    std::string key_file_str = pth + disp + "/" + disp + "-private.pem.key";
    
    // Convertir a const char* usando .c_str()
    opt.ca_file   = ca_file_str.c_str();
    opt.crt_file  = crt_file_str.c_str();
    opt.key_file  = key_file_str.c_str();
    opt.verify_peer = 1;

    std::cout << "[MQTT] Creando SSL context..." << std::endl;
    int sslRet = cli.newSslCtx(&opt);
    
    // Mensaje más descriptivo para SSL
    if (sslRet == 0) {
        std::cout << "[MQTT] SSL context creado exitosamente" << std::endl;
    } else {
        std::cout << "[MQTT] ERROR creando SSL context (código: " << sslRet << ")" << std::endl;
        std::cout << "[MQTT] Verifique los archivos de certificados en " <<pth + disp<< std::endl;
        return;
    }

    cli.setConnectTimeout(3000); 
    const char* host = "a265m6rkc34opn-ats.iot.us-east-1.amazonaws.com";
    int port = 8883;

    cli.onConnect = [](hv::MqttClient* cli) {
        std::cout << "[MQTT] CONEXIÓN EXITOSA al broker MQTT" << std::endl;
    };

    cli.onClose = [](hv::MqttClient* cli) {
        std::cout << "[MQTT] DESCONECTADO del broker, intentando reconectar..." << std::endl;
    };

    cli.onMessage = [](hv::MqttClient* cli, mqtt_message_t* msg) {
        std::cout << "[MQTT] Mensaje recibido en topic: "
                  << std::string(msg->topic, msg->topic_len) 
                  << ", tamaño: " << msg->payload_len << " bytes" << std::endl;
    };

    // Conexión inicial
    std::cout << "[MQTT] Intentando conectar a " << host << ":" << port << " con SSL..." << std::endl;
    int connRet = cli.connect(host, port, 1); // 1 = SSL
    
    // Mensajes descriptivos para la conexión
    if (connRet == 0) {
        std::cout << "[MQTT] CONEXIÓN INICIAL EXITOSA" << std::endl;
    } else {
        std::cout << "[MQTT] ERROR en conexión inicial (código: " << connRet << ")" << std::endl;
        std::cout << "[MQTT] Posibles causas: " << std::endl;
        std::cout << "[MQTT]   - Problemas de red/resolución DNS" << std::endl;
        std::cout << "[MQTT]   - Certificados SSL inválidos o expirados" << std::endl;
        std::cout << "[MQTT]   - Broker no disponible" << std::endl;
        std::cout << "[MQTT]   - Problemas de autenticación" << std::endl;
    }

    // Hilo de reconexión con mensajes mejorados
    std::thread([host, port]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(5)); // Aumentado a 5 segundos
            
            if (!cli.isConnected()) {
                std::cout << "[MQTT] INTENTANDO RECONEXIÓN..." << std::endl;
                int r = cli.connect(host, port, 1);
                
                if (r == 0) {
                    std::cout << "[MQTT] RECONEXIÓN EXITOSA" << std::endl;
                } else {
                    std::cout << "[MQTT] FALLA en reconexión (código: " << r << ")" << std::endl;
                    
                    // Mensajes específicos basados en códigos de error comunes
                    switch (r) {
                        case -1:
                            std::cout << "[MQTT] Error: Timeout de conexión" << std::endl;
                            break;
                        case -2:
                            std::cout << "[MQTT] Error: Resolución DNS fallida" << std::endl;
                            break;
                        case -3:
                            std::cout << "[MQTT] Error: Conexión rechazada" << std::endl;
                            break;
                        default:
                            std::cout << "[MQTT] Error desconocido, verifique configuración" << std::endl;
                            break;
                    }
                }
            }
        }
    }).detach();

    // Hilo principal del loop de MQTT
    std::thread([]() {
        std::cout << "[MQTT] Iniciando loop MQTT..." << std::endl;
        cli.run();
    }).detach();
}

extern "C" {
std::atomic<bool> horaRecibida(false);

#define API_STR(api, func)  "/api/" #api "/" #func
#define API_GET(api, func)  router.GET(API_STR(api, func), func)
#define API_POST(api, func) router.POST(API_STR(api, func), func)
#define ENCABEZADO   "E114010000"
#define NET_ID       "01"
#define TMSTP        "02"
#define CONT         "40"
#define CONT_TYPE    "01"
#define STATUS       "30"
#define EVENT        "21"
#define CAMIONETA    "01"
#define SAMPI        "02"
#define PERSONA      "16"
#define EPP          "32"

std::mutex              detector_mutex;
ma::Camera*             global_camera = nullptr;
ma::Model*              global_model = nullptr;
ma::engine::EngineCVI*  global_engine = nullptr;
bool isInitialized      = false;
static HttpServer       server;
std::string url_report  = "http://192.168.4.1/report";
std::string url_command = "http://192.168.4.1/command"; // Para enviar comandos (solicitud de hora)
std::string url_id      = "http://192.168.4.1/id";      // Para mandar la mac/id para luego recibir comandos

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

static void registerResponseApi(HttpService& router) {
    router.POST("/response", [](HttpRequest* req, HttpResponse* resp) {
        if (!req || !resp) {
            printf("[CRITICAL] Request o response nulos\n");
            return resp->String("Error interno");
        }

        try {
            std::string body = req->body;
            if (body.empty()) {
                return resp->String("Body vacío");
            }

            printf("[INFO] POST /response recibido: %s\n", body.c_str());

            if (body.size() == 8) {
                // Caso 1: solo epoch
                std::string epoch_hex = body;
                uint32_t epoch = std::stoul(epoch_hex, nullptr, 16);
                std::string cmd = "date -s @" + std::to_string(epoch);
                if (system(cmd.c_str()) == 0) {
                    printf("[OK] Hora actualizada con epoch %u\n", epoch);
                    return resp->String("OK");
                } else {
                    printf("[ERROR] No se pudo ejecutar date -s\n");
                    return resp->String("Error actualizando hora");
                }
            } else if (body.substr(0, 2) == "C0") {
                std::string comando = body.substr(2, 2);

                if (comando == "01" && body.size() >= 10) {
                    // Caso 2: encabezado C0 + comando 01 + epoch
                    std::string epoch_hex = body.substr(4);
                    uint32_t epoch = std::stoul(epoch_hex, nullptr, 16);
                    std::string cmd = "date -s @" + std::to_string(epoch);
                    if (system(cmd.c_str()) == 0) {
                        printf("[OK] Hora actualizada con epoch %u\n", epoch);
                        return resp->String("OK");
                    } else {
                        printf("[ERROR] No se pudo ejecutar date -s\n");
                        return resp->String("Error actualizando hora");
                    }
                } else if (comando == "02") {
                    // Caso 3: encabezado C0 + comando 02 => reboot
                    printf("[INFO] Comando reboot recibido\n");
                    system("sudo reboot");
                    return resp->String("Reiniciando...");
                } else {
                    printf("[WARN] Comando desconocido: %s\n", comando.c_str());
                    return resp->String("Comando desconocido");
                }
            } else {
                printf("[WARN] Formato de body inválido\n");
                return resp->String("Body inválido");
            }

        } catch (const std::exception& e) {
            printf("[EXCEPTION] Error: %s\n", e.what());
            return resp->String("Error interno");
        }
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
    try {
        if (4 != g_wifiMode) {
            th = std::thread(monitorWifiStatusThread);
            th.detach();
        }
    } catch (const std::exception &e) {
        syslog(LOG_ERR, "Exception in monitorWifiStatusThread: %s", e.what());
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
    // Llamar en initHttpd():
    registerResponseApi(router);


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

// Función para obtener información WiFi (SSID, RSSI e IP)
bool getWifiInfo(std::string& ssid, int& rssi, std::string& ip) {
    FILE* fp = popen("iwconfig wlan0 2>/dev/null", "r");
    if (!fp) {
        return false;
    }

    char buffer[256];
    ssid = "unknown";
    rssi = 0;
    ip = "unknown";

    // --- Leer SSID y RSSI ---
    while (fgets(buffer, sizeof(buffer), fp)) {
        // Buscar SSID
        if (strstr(buffer, "ESSID:")) {
            char* start = strchr(buffer, '\"');
            if (start) {
                char* end = strchr(start + 1, '\"');
                if (end) {
                    ssid = std::string(start + 1, end - start - 1);
                }
            }
        }

        // Buscar Signal level
        if (strstr(buffer, "Signal level=")) {
            char* level_str = strstr(buffer, "Signal level=");
            if (level_str) {
                level_str += 13; // Saltar "Signal level="
                rssi = atoi(level_str);
            }
        }
    }
    pclose(fp);

    // --- Obtener la IP asociada a wlan0 ---
    fp = popen("ip -4 addr show wlan0 | grep -oP '(?<=inet\\s)\\d+(\\.\\d+){3}'", "r");
    if (fp) {
        if (fgets(buffer, sizeof(buffer), fp)) {
            // Eliminar salto de línea al final si existe
            buffer[strcspn(buffer, "\n")] = 0;
            ip = buffer;
        }
        pclose(fp);
    }

    return true;
}

void sendDetectionByMqtt(const std::string& payload, const std::string& topic) {
    if (cli.isConnected()) {
        std::string ssid, ip, deveui;
        int rssi;
        deveui = g_cfg.deveui_concentrador;
        getWifiInfo(ssid, rssi, ip);

          nlohmann::json json_payload = {
            {"PayloadData", payload},
            {"WirelessMetadata", {
                {"WiFi", {
                    {"MacAddr", deveui},
                    {"SSID", ssid},
                    {"RSSI", rssi}
                }}
            }}
        };
        
        // Convertir a string para enviar
        std::string json_str = json_payload.dump();
        cli.publish(topic, json_str.c_str());
        printf("Mensaje enviado - SSID: %s, RSSI: %d dBm\n", ssid.c_str(), rssi);
    } else {
        printf("Cliente NO conectado\n");
        syslog(LOG_WARNING, "MQTT client not connected. Skipping publish.");
    }
}

void logToFile(const char* level, const char* msg) {
    FILE* f = fopen("logfile.txt", "a");  // "a" = append
    if (!f) return;

    // timestamp
    std::time_t now = std::time(nullptr);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));

    fprintf(f, "[%s] [%s] %s\n", buf, level, msg);
    fclose(f);
}

void sendTestHttpPost(const std::string& payload) {
    const char* url_cstr = url_report.c_str();

    printf("[INFO] Enviando prueba HTTP POST a %s\n", url_cstr);
    logToFile("INFO", ("Enviando prueba HTTP POST a " + url_report).c_str());

    printf("[DEBUG] Payload: %s\n", payload.c_str());
    logToFile("DEBUG", ("Payload: " + payload).c_str());
    
    auto resp = requests::post(url_cstr, payload, {{"Content-Type", "text/plain"}});

    if (resp == nullptr) {
        printf("[ERROR] No se recibió respuesta del servidor\n");
        logToFile("ERROR", "No se recibió respuesta del servidor");
    } else {
        printf("[INFO] Respuesta HTTP %d - Contenido: %s\n", resp->status_code, resp->body.c_str());
        
        std::string logMsg = "Respuesta HTTP " + std::to_string(resp->status_code) + 
                             " - Contenido: " + resp->body;
        logToFile("INFO", logMsg.c_str());
    }
}

void sendMacId() {
    std::string mac = g_cfg.mac;
    std::string payload = "0000" + mac;
    const int timeout_seconds = 10;

    printf("[INFO] Enviando MAC a %s\n", url_id.c_str());
    printf("[DEBUG] Payload: %s\n", payload.c_str());

    auto start_time = std::chrono::steady_clock::now();
    auto resp = requests::post(url_id.c_str(), payload, {{"Content-Type", "text/plain"}});
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - start_time
    ).count();

    if (resp == nullptr || elapsed > timeout_seconds) {
        printf("[WARN] Sin respuesta o timeout (%lds). Reintentando...\n", elapsed);
        std::this_thread::sleep_for(std::chrono::seconds(20));  // pequeña pausa
        resp = requests::post(url_id.c_str(), payload, {{"Content-Type", "text/plain"}});
    }

    if (resp == nullptr) {
        printf("[ERROR] No se recibió respuesta del servidor después del reintento\n");
    } else {
        printf("[INFO] Respuesta HTTP %d - Contenido: %s\n", resp->status_code, resp->body.c_str());
    }
}

void solicitarHora() {
    const char* url_cs = url_report.c_str();
    std::string payload_cs = "FF";
    const char* url_cstr = url_command.c_str();
    std::string payload = "E11401800009000100000000";
    MA_LOGD(TAG, "Enviando Solicitud de hora POST");
    printf("[INFO] Enviando Solicitud de hora POST a %s\n", url_cstr);
    printf("[DEBUG] Payload de hora: %s\n", payload.c_str());

    auto resp1 = requests::post(url_cstr, payload, {{"Content-Type", "text/plain"}});
    auto resp2 = requests::post(url_cs, payload_cs, {{"Content-Type", "text/plain"}});
    if (resp1 == nullptr) {
        printf("[ERROR] No se recibió respuesta del servidor\n");
        logToFile("ERROR", "No se recibió respuesta del servidor");
        return;
    }

    printf("[INFO] Respuesta HTTP %d - Contenido: %s\n", resp1->status_code, resp1->body.c_str());
    logToFile("INFO", ("Respuesta HTTP " + std::to_string(resp1->status_code) + 
                       " - Contenido: " + resp1->body).c_str());

    // Ya no procesamos aquí la hora; esperamos que el gateway haga POST /response
    if (resp1->body.find("OK") != std::string::npos) {
        printf("[INFO] Solicitud enviada correctamente, esperando POST /response...\n");
    } else {
        printf("[ERROR] Respuesta de hora inválida: %s\n", resp1->body.c_str());
        logToFile("ERROR", ("Respuesta de hora inválida: " + resp1->body).c_str());
    }
}

std::string timestampToHexString(uint64_t timestamp) {
    std::stringstream hex_stream;
    uint32_t timestamp_32 = static_cast<uint32_t>(timestamp);
    hex_stream << std::setw(8) << std::setfill('0') << std::hex << std::uppercase << timestamp_32;
    return hex_stream.str();
}

struct AlarmConfig {
    std::string json_key; 
    std::string topic;     
    std::chrono::steady_clock::time_point* last_alert; 
};

void process_detection_results(nlohmann_json& data, uint8_t* frame,
    std::chrono::steady_clock::time_point& last_obj_alert,
    std::chrono::steady_clock::time_point& last_zone_alert,
    std::chrono::steady_clock::time_point& last_report) 
{
    using clk = std::chrono::steady_clock;
    auto now = clk::now();
    uint64_t ts = getTimestamp();

    // Estado de detección de camioneta
    static clk::time_point last_cam_det;
    static bool cam_active = false;
    static bool first_cam = true;

    std::string st_val = "00";

    try {
        // Detección de camioneta
        if (data.contains("camioneta")) {
            auto secs = std::chrono::duration_cast<std::chrono::seconds>(now - last_cam_det).count();

            last_cam_det = now;
            cam_active = true;

            if (first_cam || secs >= 5 * 60) {
                first_cam = false;

                std::string payload = std::string(ENCABEZADO) + NET_ID + "0000" + g_cfg.mac +
                    TMSTP + timestampToHexString(ts) +
                    EVENT + CAMIONETA;

                MA_LOGD(TAG, "Alerta camioneta: %s", payload.c_str());
                if (internal_mode == CONNECTIVITY_MODE_MQTT)
                    sendDetectionByMqtt(payload, ("industria4-0/devices/" + g_cfg.disp + "/events").c_str());
                else
                    sendTestHttpPost(payload);
            } else {
                MA_LOGD(TAG, "Camioneta detectada en cooldown");
            }
        }

        // Actualizar estado de camioneta, cada 5 minutos
        if (cam_active) {
            auto idle = std::chrono::duration_cast<std::chrono::seconds>(now - last_cam_det).count();
            if (idle >= 5 * 60) {
                cam_active = false;
                st_val = "00";
                std::cout << "Estado camioneta reseteado (sin detección > 5min)\n";
            } else st_val = CAMIONETA;
        }

        // Reporte periódico de estado
        auto since_report = std::chrono::duration_cast<std::chrono::seconds>(now - last_report).count();
        if (since_report >= g_cfg.cooldown && data.contains("person_count") && g_cfg.reporte_personas) {
            last_report = now;
            int count = data["person_count"].get<int>();

            std::stringstream ss;
            ss << std::setw(4) << std::setfill('0') << std::hex << count;

            std::string payload = std::string(ENCABEZADO) + NET_ID + "0000" + g_cfg.mac +
                TMSTP + timestampToHexString(ts) +
                CONT + CONT_TYPE + ss.str() +
                STATUS + st_val;

            MA_LOGD(TAG, "Reporte: %s (personas=%d, estado=%s)", payload.c_str(), count, st_val.c_str());
            if (internal_mode == CONNECTIVITY_MODE_MQTT)
                sendDetectionByMqtt(payload, ("industria4-0/devices/" + g_cfg.disp + "/status").c_str());
            else
                sendTestHttpPost(payload);
        }

    } catch (const nlohmann_json::exception& e) {
        MA_LOGE(TAG, "Error JSON: %s", e.what());
    } catch (const std::exception& e) {
        MA_LOGE(TAG, "Error inesperado: %s", e.what());
    }
}

void initConnectivity(connectivity_mode_t& connectivity_mode) {

    if (connectivity_mode == CONNECTIVITY_MODE_MQTT){
        std::thread th;
        MA_LOGI("System", "Iniciando modo MQTT...");
        th           = std::thread(initMqtt);
        th.detach();
        internal_mode = 0;
    } 
    if (connectivity_mode == CONNECTIVITY_MODE_HTTP){
        MA_LOGI("System", "Iniciando modo HTTP...");
        internal_mode = 1;
    } 
}

}  // extern "C" {