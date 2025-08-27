struct Config {
    // eventos
    std::string ev_no_helmet;
    std::string ev_zone;
    std::string code_no_helmet;
    std::string code_zone;
    std::string code_person;

    // MQTT / HTTP
    std::string mqtt_ev_topic;
    std::string mqtt_st_topic;
    std::string http_url;

    // timers
    int cooldown_ms;
    int report_ms;

    // paths
    std::string dir_images;
    std::string model_yolo;
    std::string ssl_certs;

    // detección
    float conf_thresh;
    float nms_thresh;
    int infer_w;
    int infer_h;

    // zona
    bool zone_enabled;
    std::string zone_type;
    float zone_div;

    float line_x1, line_y1, line_x2, line_y2;
    float rect_x1, rect_y1, rect_x2, rect_y2;

    int zone_r, zone_g, zone_b;
    int zone_thick;

    // almacenamiento
    int max_images;

    // clases
    std::string cls_no_helmet;
    std::string cls_person;

};
bool loadConfig(const std::string& filename);
extern Config g_cfg;
