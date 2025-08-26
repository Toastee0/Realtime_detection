#ifndef _GLOBAL_CFG_H_
#define _GLOBAL_CFG_H_

/* HTTPD */
#define HTTPD_PORT          80
#define HTTPS_PORT          443
#define TTYD_PORT           9090
#define WWW(file)           ("/usr/share/supervisor/www/" file)
#define REDIRECT_URL        "http://192.168.16.1/index.html"
#define DEFAULT_UPGRADE_URL "https://github.com/Seeed-Studio/reCamera-OS/releases/latest"

/* WebSocket */
#define WS_PORT                    8000

#define PATH_MODEL                 "/proc/device-tree/model"
#define PATH_ISSUE                 "/etc/issue"
#define PATH_SECRET                "/etc/secret"
#define PATH_DEVICE_NAME           "/etc/device-name"
#define PATH_UPGRADE_URL           "/etc/upgrade"
#define PATH_SSH_KEY_FILE          "/home/recamera/.ssh/authorized_keys"
#define PATH_TMP_KEY_FILE          "/tmp/sshkey.tmp"
#define PATH_UPGRADE_PROGRESS_FILE "/tmp/upgrade.percentage"
#define PATH_UPGRADE_VERSION_FILE  "/tmp/upgrade.version"
#define PATH_MODEL_DOWNLOAD_DIR    "/userdata/MODEL/"
#define PATH_APP_DOWNLOAD_DIR      "/userdata/app/"
#define PATH_MODEL_UPGRADE_DIR     "/userdata/upgrade/"
#define PATH_MODEL_LIST_DIR        "/usr/share/supervisor/models/"
#define PATH_PLATFORM_INFO_FILE    "/usr/share/supervisor/platform.info"

#define PATH_AVAHI_CONF            "/etc/avahi/avahi-daemon.conf"
#define PATH_AVAHI_DAEMON_SERVICE  "/etc/init.d/S50avahi-daemon"
#define PATH_HOSTAPD_CONF          "/etc/hostapd_2g4.conf"

#define PATH_NODERED_CONF          "/home/recamera/.node-red/settings.js"

#define PATH_SERVER_CRT            "/etc/cert/server.crt"
#define PATH_SERVER_KEY            "/etc/cert/server.key"

#define PATH_FIRST_LOGIN           "/etc/.first_login"
#define PATH_SSHD                  "/etc/init.d/S50sshd"

#define PATH_WLAN0_MAC             "/sys/class/net/wlan0/address"

#define KEY_AES_128                "zqCwT7H7!rNdP3wL"

/* usertool.sh*/
#define SCRIPT_USER(action)                      \
    ("/usr/share/supervisor/scripts/usertool.sh" \
     " " #action " ")
#define SCRIPT_USER_ID         SCRIPT_USER(id)
#define SCRIPT_USER_NAME       SCRIPT_USER(name)
#define SCRIPT_USER_VERIFY     SCRIPT_USER(verify)
#define SCRIPT_USER_PWD        SCRIPT_USER(passwd)
#define SCRIPT_USER_SAVE       SCRIPT_USER(save)
#define SCRIPT_USER_SSH        SCRIPT_USER(query_key)
#define SCRIPT_USER_VERIFY_SSH SCRIPT_USER(verify_key)

/* wifitool.sh*/
#define SCRIPT_WIFI(action)                      \
    ("/usr/share/supervisor/scripts/wifitool.sh" \
     " " #action " ")
#define SCRIPT_WIFI_START          SCRIPT_WIFI(start)
#define SCRIPT_WIFI_STOP           SCRIPT_WIFI(stop)
#define SCRIPT_WIFI_START_AP       SCRIPT_WIFI(start_ap)
#define SCRIPT_WIFI_STOP_AP        SCRIPT_WIFI(stop_ap)
#define SCRIPT_WIFI_SCAN           SCRIPT_WIFI(scan)
#define SCRIPT_WIFI_SCAN_RESULTS   SCRIPT_WIFI(scan_results)
#define SCRIPT_WIFI_LIST           SCRIPT_WIFI(list)
#define SCRIPT_WIFI_CONNECT        SCRIPT_WIFI(connect)
#define SCRIPT_WIFI_CONNECT_STATUS SCRIPT_WIFI(connect_status)
#define SCRIPT_WIFI_AUTO_CONNECT   SCRIPT_WIFI(auto_connect)
#define SCRIPT_WIFI_GET_WIFI_ID    SCRIPT_WIFI(get_wifi_id)
#define SCRIPT_WIFI_GET_WIFI_IP    SCRIPT_WIFI(get_wifi_ip)
#define SCRIPT_WIFI_SELECT         SCRIPT_WIFI(select)
#define SCRIPT_WIFI_DISCONNECT     SCRIPT_WIFI(disconnect)
#define SCRIPT_WIFI_STATUS         SCRIPT_WIFI(status)
#define SCRIPT_WIFI_REMOVE         SCRIPT_WIFI(remove)
#define SCRIPT_WIFI_STATE          SCRIPT_WIFI(state)
#define SCRIPT_WIFI_GATEWAY        SCRIPT_WIFI(get_gateway)

/* upgrade.sh */
#define SCRIPT_UPGRADE(action)                  \
    ("/usr/share/supervisor/scripts/upgrade.sh" \
     " " #action " ")
#define SCRIPT_UPGRADE_LATEST   SCRIPT_UPGRADE(latest)
#define SCRIPT_UPGRADE_DOWNLOAD SCRIPT_UPGRADE(download)
#define SCRIPT_UPGRADE_START    SCRIPT_UPGRADE(start)
#define SCRIPT_UPGRADE_QUERY    SCRIPT_UPGRADE(query)
#define SCRIPT_UPGRADE_STOP     SCRIPT_UPGRADE(stop)

/* devicetool.sh */
#define SCRIPT_DEVICE(action)                      \
    ("/usr/share/supervisor/scripts/devicetool.sh" \
     " " #action " ")
#define SCRIPT_DEVICE_GETSYSTEMSTATUS SCRIPT_DEVICE(getSystemStatus)
#define SCRIPT_DEVICE_GETSNCODE       SCRIPT_DEVICE(getSnCode)
#define SCRIPT_DEVICE_GETUPDATESTATUS SCRIPT_DEVICE(getUpdateStatus)
#define SCRIPT_DEVICE_GETADDRESSS     SCRIPT_DEVICE(getAddress)
#define SCRIPT_DEVICE_INSTALLAPP      SCRIPT_DEVICE(installApp)
#define SCRIPT_DEVICE_GETAPPINFO      SCRIPT_DEVICE(getAppInfo)
#define SCRIPT_DEVICE_GETFILEMD5      SCRIPT_DEVICE(getFileMd5)
#define SCRIPT_DEVICE_RESTARTNODERED  SCRIPT_DEVICE(restartNodered)
#define SCRIPT_DEVICE_RESTARTSSCMA    SCRIPT_DEVICE(restartSscma)
#define SCRIPT_DEVICE_ENABLE_SSHD     SCRIPT_DEVICE(enableSshd)


/* Configuración de eventos y alertas */
#define EVENT_DETECTED_NO_HELMET          "detected_no_helmet"
#define EVENT_RESTRICTED_ZONE             "restricted_zone"
#define EVENT_CODE_NO_HELMET              "E1140140002103"
#define EVENT_CODE_ZONE_VIOLATION         "E1140140002102"
#define EVENT_CODE_PERSON_COUNT           "E1140100004001"  
#define MQTT_TOPIC_EVENTS                 "industria4-0/devices/cam_1/events"
#define MQTT_TOPIC_STATUS                 "industria4-0/devices/cam_1/status" 
#define HTTP_SERVER_URL                   "http://192.168.4.1/report"

/* Tiempos de alerta (en milisegundos) */
#define ALERT_COOLDOWN_MS                 5000
#define PERSON_REPORT_INTERVAL_MS         5000

/* Rutas de modelos y archivos */
#define PATH_IMAGES_DIR                   "/usr/local/bin/images"
#define PATH_MODEL_YOLO                   "/usr/local/bin/yolo11n_helmetPerson_int8_sym.cvimodel"
#define PATH_SSL_CERTS                    "/etc/ssl/certs/cam_1/"

/* Configuración de video */
#define VIDEO_FORMAT_DEFAULT              VIDEO_FORMAT_RGB888
#define VIDEO_WIDTH_DEFAULT               1920
#define VIDEO_HEIGHT_DEFAULT              1080
#define VIDEO_FPS_DEFAULT                 10
#define VISUAL_ZONE_DIVIDER_COLOR cv::Scalar(0, 0, 255)
#define VISUAL_BBOX_THICKNESS 3
#define VISUAL_ZONE_DIVIDER_THICKNESS 2

/* Configuración de detección */
#define DETECTION_CONFIDENCE_THRESHOLD    0.5f
#define DETECTION_NMS_THRESHOLD           0.4f
#define DETECTION_INFERENCE_WIDTH         640
#define DETECTION_INFERENCE_HEIGHT        640

/* Zona restringida */
#define ZONE_ENABLED                      true

#define ZONE_TYPE "LINE"
#define ZONE_DIVIDER_PERCENT 0.75f

#define ZONE_TYPE "LINE_ADVANCED"
#define ZONE_LINE_START_X_PERCENT 0.5f   // 50% izquierda
#define ZONE_LINE_START_Y_PERCENT 0.0f   // Parte superior
#define ZONE_LINE_END_X_PERCENT   1.0f   // 100% derecha  
#define ZONE_LINE_END_Y_PERCENT   1.0f   // Parte inferior

#define ZONE_TYPE "RECTANGLE"
#define ZONE_RECT_TOP_LEFT_X_PERCENT      0.7f
#define ZONE_RECT_TOP_LEFT_Y_PERCENT      0.2f  
#define ZONE_RECT_BOTTOM_RIGHT_X_PERCENT  0.9f
#define ZONE_RECT_BOTTOM_RIGHT_Y_PERCENT  0.8f

#define ZONE_COLOR_R                      0
#define ZONE_COLOR_G                      0
#define ZONE_COLOR_B                      255
#define ZONE_THICKNESS                    2

/* Límites de almacenamiento */
#define MAX_IMAGES_STORED               1000   // Máximo de imágenes almacenadas
#define MAX_STORAGE_SIZE_MB             500    // Máximo espacio en disco en MB

#define DETECTION_CLASS_NO_HELMET         "no_helmet"
#define DETECTION_CLASS_PERSON            "person"

/* Modos de conectividad */
typedef enum {
    CONNECTIVITY_MODE_MQTT = 0,
    CONNECTIVITY_MODE_HTTP = 1
} connectivity_mode_t;

#endif
