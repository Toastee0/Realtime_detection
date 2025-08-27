#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <algorithm>
#include "config.h"

Config g_cfg;

// Función auxiliar para quitar espacios
std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end = s.find_last_not_of(" \t\r\n");
    return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

// Cargar config.ini
bool loadConfig(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) return false;

    std::map<std::string, std::string> ini;
    std::string line, current_section;

    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';') continue;

        if (line.front() == '[' && line.back() == ']') {
            current_section = line.substr(1, line.size() - 2);
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));
        ini[current_section + "." + key] = value;
    }

    // Eventos
    g_cfg.ev_no_helmet   = ini["events.ev_no_helmet"];
    g_cfg.ev_zone        = ini["events.ev_zone"];
    g_cfg.code_no_helmet = ini["events.code_no_helmet"];
    g_cfg.code_zone      = ini["events.code_zone"];
    g_cfg.code_person    = ini["events.code_person"];

    // MQTT / HTTP
    g_cfg.mqtt_ev_topic = ini["mqtt.mqtt_ev_topic"];
    g_cfg.mqtt_st_topic = ini["mqtt.mqtt_st_topic"];
    g_cfg.http_url      = ini["http.http_url"];

    // Timers
    g_cfg.cooldown_ms = std::stoi(ini["timers.cooldown_ms"]);
    g_cfg.report_ms   = std::stoi(ini["timers.report_ms"]);

    // Paths
    g_cfg.dir_images  = ini["paths.dir_images"];
    g_cfg.model_yolo  = ini["paths.model_yolo"];
    g_cfg.ssl_certs   = ini["paths.ssl_certs"];

    // Detección
    g_cfg.conf_thresh   = std::stof(ini["detection.conf_thresh"]);
    g_cfg.nms_thresh    = std::stof(ini["detection.nms_thresh"]);
    g_cfg.infer_w       = std::stoi(ini["detection.infer_w"]);
    g_cfg.infer_h       = std::stoi(ini["detection.infer_h"]);
    g_cfg.cls_no_helmet = ini["detection.cls_no_helmet"];
    g_cfg.cls_person    = ini["detection.cls_person"];

    // Zona
    g_cfg.zone_enabled = (ini["zone.zone_enabled"] == "true");
    g_cfg.zone_type    = ini["zone.zone_type"];
    g_cfg.zone_div     = std::stof(ini["zone.zone_div"]);

    g_cfg.line_x1 = std::stof(ini["zone.line_x1"]);
    g_cfg.line_y1 = std::stof(ini["zone.line_y1"]);
    g_cfg.line_x2 = std::stof(ini["zone.line_x2"]);
    g_cfg.line_y2 = std::stof(ini["zone.line_y2"]);

    g_cfg.rect_x1 = std::stof(ini["zone.rect_x1"]);
    g_cfg.rect_y1 = std::stof(ini["zone.rect_y1"]);
    g_cfg.rect_x2 = std::stof(ini["zone.rect_x2"]);
    g_cfg.rect_y2 = std::stof(ini["zone.rect_y2"]);

    g_cfg.zone_r     = std::stoi(ini["zone.zone_r"]);
    g_cfg.zone_g     = std::stoi(ini["zone.zone_g"]);
    g_cfg.zone_b     = std::stoi(ini["zone.zone_b"]);
    g_cfg.zone_thick = std::stoi(ini["zone.zone_thick"]);

    // Storage
    g_cfg.max_images = std::stoi(ini["storage.max_images"]);

    return true;
}
