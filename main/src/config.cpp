#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <algorithm>
#include <iostream>
#include <vector>
#include "config.h"

Config g_cfg;

std::string get_mac_address(const std::string& iface = "wlan0") {
    std::ifstream file("/sys/class/net/" + iface + "/address");
    if (!file.is_open()) {
        std::cerr << "ERROR: No se pudo abrir la dirección MAC de " << iface << std::endl;
        return "";
    }
    std::string mac;
    std::getline(file, mac);
    file.close();

    // remover ':'
    mac.erase(std::remove(mac.begin(), mac.end(), ':'), mac.end());

    // pasar a mayúsculas
    std::transform(mac.begin(), mac.end(), mac.begin(), ::toupper);

    return mac;
}

bool update_config_mac(const std::string& filename, const std::string& mac) {
    std::ifstream infile(filename);
    if (!infile.is_open()) return false;

    std::stringstream buffer;
    std::string line;
    while (std::getline(infile, line)) {
        if (line.find("mac") != std::string::npos && line.find("=") != std::string::npos) {
            buffer << "mac = " << mac << "\n";
        } else {
            buffer << line << "\n";
        }
    }
    infile.close();

    std::ofstream outfile(filename, std::ios::trunc);
    if (!outfile.is_open()) return false;

    outfile << buffer.str();
    outfile.close();
    return true;
}
// Función auxiliar para quitar espacios
std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end = s.find_last_not_of(" \t\r\n");
    return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

std::vector<float> parse_float_list(const std::string& str) {
    std::vector<float> result;
    std::stringstream ss(str);
    std::string item;
    
    while (std::getline(ss, item, ',')) {
        try {
            result.push_back(std::stof(trim(item)));
        } catch (...) {
            std::cout << "ERROR: Valor no numérico en lista: '" << item << "'" << std::endl;
        }
    }
    return result;
}

std::vector<std::string> parse_string_list(const std::string& str) {
    std::vector<std::string> result;
    std::stringstream ss(str);
    std::string item;

    while (std::getline(ss, item, ',')) {
        std::string trimmed = trim(item);
        if (!trimmed.empty()) {
            result.push_back(trimmed);
        }
    }
    return result;
}

// Función para verificar tipos de zona válidos
bool is_valid_zone_type(const std::string& type) {
    const std::vector<std::string> validTypes = {
        "LINE", "LINE_ADV", "RECT", "POLY", "NONE"
    };
    return std::find(validTypes.begin(), validTypes.end(), type) != validTypes.end();
}

// Función para verificar coordenadas según el tipo de zona
bool validate_zone_coords(const std::string& zoneType, const std::vector<float>& coords) {
    if (zoneType == "LINE") {
        if (coords.size() < 1) {
            std::cout << "ERROR: LINE necesita al menos 1 valor (zone_div)" << std::endl;
            return false;
        }
        if (coords[0] < 0.0f || coords[0] > 1.0f) {
            std::cout << "ERROR: zone_div debe estar entre 0.0 y 1.0" << std::endl;
            return false;
        }
        
    } else if (zoneType == "LINE_ADV") {
        if (coords.size() < 4) {
            std::cout << "ERROR: LINE_ADV necesita 4 valores (line_x1, line_y1, line_x2, line_y2)" << std::endl;
            return false;
        }
        for (int i = 0; i < 4; i++) {
            if (coords[i] < 0.0f || coords[i] > 1.0f) {
                std::cout << "ERROR: Coordenadas LINE_ADVANCED deben estar entre 0.0 y 1.0" << std::endl;
                return false;
            }
        }
        
    } else if (zoneType == "RECT") {
        if (coords.size() < 4) {
            std::cout << "ERROR: RECT necesita 4 valores (rect_x1, rect_y1, rect_x2, rect_y2)" << std::endl;
            return false;
        }
        for (int i = 0; i < 4; i++) {
            if (coords[i] < 0.0f || coords[i] > 1.0f) {
                std::cout << "ERROR: Coordenadas RECTANGLE deben estar entre 0.0 y 1.0" << std::endl;
                return false;
            }
        }
        if (coords[0] >= coords[2] || coords[1] >= coords[3]) {
            std::cout << "ADVERTENCIA: Coordenadas de rectángulo podrían estar invertidas (x1<x2, y1<y2)" << std::endl;
        }
        
    } else if (zoneType == "POLY") {
        if (coords.size() < 6 || coords.size() % 2 != 0) {
            std::cout << "ERROR: POLY necesita mínimo 3 puntos (6 valores) y cantidad par de coordenadas" << std::endl;
            return false;
        }
        for (size_t i = 0; i < coords.size(); i++) {
            if (coords[i] < 0.0f || coords[i] > 1.0f) {
                std::cout << "ERROR: Coordenadas POLYGON deben estar entre 0.0 y 1.0" << std::endl;
                return false;
            }
        }
    }
    
    return true;
}

// Cargar config.ini con validaciones
bool load_config(const std::string& filename) {
    std::cout << "Cargando configuración desde: " << filename << std::endl;
    
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cout << "ERROR: No se pudo abrir el archivo de configuración: " << filename << std::endl;
        return false;
    }

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

    // ===== VALIDACIONES Y CARGA DE DATOS =====

    try {
        g_cfg.mac = ini["device.mac"];
        g_cfg.deveui_concentrador = ini["device.deveui_concentrador"];
        if (g_cfg.mac.empty() || g_cfg.mac == "000000000000") {
            std::cout << "MAC no definida en config. Leyendo desde sistema..." << std::endl;
            std::string mac = get_mac_address("wlan0");
            if (!mac.empty()) {
                g_cfg.mac = mac;
                if (update_config_mac(filename, mac)) {
                    std::cout << "MAC detectada y guardada: " << g_cfg.mac << std::endl;
                } else {
                    std::cout << "ERROR: No se pudo actualizar config.ini con la MAC" << std::endl;
                    update_config_mac(filename, "000000000000");
                }
            } else {
                std::cout << "ERROR: No se pudo leer la MAC del sistema" << std::endl;
                update_config_mac(filename, "000000000000");
            }
        } else {
            std::cout << "MAC cargada desde config.ini: " << g_cfg.mac << std::endl;
        }
    } catch (...) {
        std::cout << "ERROR: Configuración inválida en [device]" << std::endl;
    }    

    // MQTT / HTTP
    try {
        g_cfg.http_url      = ini["http.http_url"];
        std::cout << "Comunicación configurada correctamente" << std::endl;
    } catch (...) {
        std::cout << "ERROR: Falta configuración en sección [mqtt] o [http]" << std::endl;
    }

    // Timers
    try {
        g_cfg.cooldown = std::stoi(ini["timers.cooldown"]);
        g_cfg.report   = std::stoi(ini["timers.report"]);

        g_cfg.time_sync_interval   = std::stoi(ini["timers.time_sync_interval"]);
        if (g_cfg.cooldown < 5) {
            std::cout << "ADVERTENCIA: cooldown muy bajo (" << g_cfg.cooldown << "ms). Mínimo recomendado: 5s" << std::endl;
        }
        std::cout << "Timers configurados: cooldown=" << g_cfg.cooldown << "s, report=" << g_cfg.report<< "s" << std::endl;
    } catch (...) {
        std::cout << "ERROR: Configuración inválida en [timers]" << std::endl;
        return false;
    }

    // Paths
    try {
        g_cfg.disp           = ini["paths.disp"];
        std::cout << "Paths configurados correctamente" << std::endl;
    } catch (...) {
        std::cout << "ERROR: Falta configuración en sección [paths]" << std::endl;
    }

    // Detección
    try {
        g_cfg.cls_detect  = parse_string_list(ini["detection.cls_detect"]);
        g_cfg.reporte_personas  = (ini["detection.reporte_personas"] == "true");
        g_cfg.toma_cap  = (ini["detection.toma_cap"] == "true");
        g_cfg.centro              = (ini["detection.centro"] == "true");
    } catch (...) {
        std::cout << "ERROR: Configuración inválida en [detection]" << std::endl;
        return false;
    }

    try {
        g_cfg.zone_enabled = (ini["zone.zone_enabled"] == "true");
        g_cfg.zone_type    = ini["zone.zone_type"];
        
        // Validar tipo de zona
        if (!is_valid_zone_type(g_cfg.zone_type)) {
            std::cout << "ERROR: Tipo de zona inválido: '" << g_cfg.zone_type << "'" << std::endl;
            std::cout << "Tipos válidos: LINE, LINE_ADV, RECT, POLY, NONE" << std::endl;
            return false;
        }
        
        // Parsear y validar coordenadas
        if (g_cfg.zone_type != "NONE") {
            g_cfg.z_coords = parse_float_list(ini["zone.z_coords"]);
            if (!validate_zone_coords(g_cfg.zone_type, g_cfg.z_coords)) {
                return false;
            }
        }
        

        // ==============================
        // NUEVOS PARÁMETROS
        // ==============================
        g_cfg.side        = ini["zone.side"];
        g_cfg.orientation = ini["zone.orientation"];

        // Validación de side según zone_type
        if (g_cfg.zone_type == "LINE" || g_cfg.zone_type == "LINE_ADV") {
            const std::vector<std::string> valid_sides = {"ARRIBA", "ABAJO", "IZQ", "DER"};
            if (std::find(valid_sides.begin(), valid_sides.end(), g_cfg.side) == valid_sides.end()) {
                std::cout << "ERROR: side inválido para " << g_cfg.zone_type 
                          << ". Valores válidos: ARRIBA, ABAJO, IZQ, DER" << std::endl;
                return false;
            }

            const std::vector<std::string> valid_orient = {"VERT", "HORIZ"};
            if (std::find(valid_orient.begin(), valid_orient.end(), g_cfg.orientation) == valid_orient.end()) {
                std::cout << "ERROR: orientation inválido. Valores válidos: VERT, HORIZ" << std::endl;
                return false;
            }
        } 
        else if (g_cfg.zone_type == "RECT" || g_cfg.zone_type == "POLY") {
            const std::vector<std::string> valid_sides = {"DENTRO", "FUERA"};
            if (std::find(valid_sides.begin(), valid_sides.end(), g_cfg.side) == valid_sides.end()) {
                std::cout << "ERROR: side inválido para " << g_cfg.zone_type 
                          << ". Valores válidos: DENTRO, FUERA" << std::endl;
                return false;
            }
            // orientation no aplica
            g_cfg.orientation = "NONE";
        }
        else if (g_cfg.zone_type == "NONE") {
            g_cfg.side = "NONE";
            g_cfg.orientation = "NONE";
        }

        std::cout << "Zona configurada: tipo=" << g_cfg.zone_type 
                  << ", side=" << g_cfg.side 
                  << ", orientation=" << g_cfg.orientation 
                  << ", habilitada=" << g_cfg.zone_enabled << std::endl;

    } catch (const std::exception& e) {
        std::cout << "ERROR en configuración de zona: " << e.what() << std::endl;
        return false;
    }
    // Storage
    try {
        g_cfg.max_images = std::stoi(ini["storage.max_images"]);
        g_cfg.train = (ini["storage.train"] == "true");
        if (g_cfg.max_images < 10) {
            std::cout << "ADVERTENCIA: max_images muy bajo (" << g_cfg.max_images << "). Mínimo recomendado: 10" << std::endl;
        }
        std::cout << "Storage configurado: max_images=" << g_cfg.max_images << std::endl;
    } catch (...) {
        std::cout << "ERROR: Configuración inválida en [storage]" << std::endl;
        return false;
    }

    std::cout << "Configuración cargada exitosamente!" << std::endl;
    return true;
}