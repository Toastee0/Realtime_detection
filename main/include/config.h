#include <sstream>
#include <vector>

struct Config {
    // devices
    std::string mac;
    std::string deveui_concentrador;

    // ¿ HTTP
    std::string http_url;

    // timers
    int cooldown;
    int report;
    int time_sync_interval;

    // paths
    std::string disp;

    // zona
    bool zone_enabled;
    std::string zone_type;
    std::vector<float> z_coords;

    std::string side;        // ARRIBA, ABAJO, IZQ, DER, DENTRO, FUERA
    std::string orientation; // VERT, HORIZ (solo aplica a LINE y LINE_ADV)

    int zone_r, zone_g, zone_b;
    int zone_thick;

    // almacenamiento
    int max_images;
    bool train;
    // clases
    std::vector<std::string>  cls_detect;
    bool reporte_personas;
    bool toma_cap;
    bool centro;
};

bool load_config(const std::string& filename);
extern Config g_cfg;
