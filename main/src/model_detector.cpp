#include <chrono>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdio.h>
#include <string>
#include <opencv2/opencv.hpp>
#include <sscma.h>
#include <vector>
#include <thread>
#include <iterator>
#include <ClassMapper.h>   
#include <unistd.h>
#include <filesystem> 
#include <nlohmann/json.hpp>
#include "global_cfg.h"
using json = nlohmann::json;
#include "cvi_sys.h"
#include "cvi_comm_video.h"
#include <regex>
#include "config.h"

class ColorPalette {
public:
    static std::vector<cv::Scalar> getPalette() {
        return palette;
    }

    static cv::Scalar getColor(int index) {
        return palette[index % palette.size()];
    }

private:
    static const std::vector<cv::Scalar> palette;
};

const std::vector<cv::Scalar> ColorPalette::palette = {
    cv::Scalar(0, 255, 0),     cv::Scalar(0, 170, 255), cv::Scalar(0, 128, 255), cv::Scalar(0, 64, 255),  cv::Scalar(0, 0, 255),     cv::Scalar(170, 0, 255),   cv::Scalar(128, 0, 255),
    cv::Scalar(64, 0, 255),    cv::Scalar(0, 0, 255),   cv::Scalar(255, 0, 170), cv::Scalar(255, 0, 128), cv::Scalar(255, 0, 64),    cv::Scalar(255, 128, 0),   cv::Scalar(255, 255, 0),
    cv::Scalar(128, 255, 0),   cv::Scalar(0, 255, 128), cv::Scalar(0, 255, 255), cv::Scalar(0, 128, 128), cv::Scalar(128, 0, 255),   cv::Scalar(255, 0, 255),   cv::Scalar(128, 128, 255),
    cv::Scalar(255, 128, 128), cv::Scalar(255, 64, 64), cv::Scalar(64, 255, 64), cv::Scalar(64, 64, 255), cv::Scalar(128, 255, 255), cv::Scalar(255, 255, 128),
};

const std::vector<std::string> ClassMapper::classes = {
    "helmet",     // ID 0
    "no_helmet",  // ID 1
    "nose",       // ID 2
    "person",     // ID 3
    "vest"        // ID 4
};

cv::Mat preprocessImage(cv::Mat& image, ma::Model* model) {
    int ih = image.rows;
    int iw = image.cols;
    int oh = 0;
    int ow = 0;

    if (model->getInputType() == MA_INPUT_TYPE_IMAGE) {
        oh = reinterpret_cast<const ma_img_t*>(model->getInput())->height;
        ow = reinterpret_cast<const ma_img_t*>(model->getInput())->width;
    }

    cv::Mat resizedImage;
    double resize_scale = std::min((double)oh / ih, (double)ow / iw);
    int nh              = (int)(ih * resize_scale);
    int nw              = (int)(iw * resize_scale);
    cv::resize(image, resizedImage, cv::Size(nw, nh));
    int top    = (oh - nh) / 2;
    int bottom = (oh - nh) - top;
    int left   = (ow - nw) / 2;
    int right  = (ow - nw) - left;

    cv::Mat paddedImage;
    cv::copyMakeBorder(resizedImage, paddedImage, top, bottom, left, right, cv::BORDER_CONSTANT, cv::Scalar::all(0));
    cv::cvtColor(paddedImage, paddedImage, cv::COLOR_BGR2RGB);

    return paddedImage;
}

void release_camera(ma::Camera*& camera) noexcept {
    
    camera->stopStream();
    camera = nullptr; // 确保线程安全
}

void release_model(ma::Model*& model, ma::engine::EngineCVI*& engine) {
    if (model) ma::ModelFactory::remove(model);  // 先释放model
    if (engine) delete engine; 

    model = nullptr;
    engine = nullptr;
}

ma::Camera* initialize_camera() noexcept{

    ma::Device* device = ma::Device::getInstance();
    ma::Camera* camera = nullptr;

    for (auto& sensor : device->getSensors()) {
        if (sensor->getType() == ma::Sensor::Type::kCamera) {
            camera = static_cast<ma::Camera*>(sensor);
            break;
        }
    }
    if (!camera) {
        MA_LOGE(TAG, "No camera sensor found");
        return camera;
    }
    if (camera->init(0) != MA_OK) {  // 假设0是默认模式
        MA_LOGE(TAG, "Camera initialization failed");
        return camera;
    }

    ma::Camera::CtrlValue value;
    value.i32 = 0;
    if (camera->commandCtrl(ma::Camera::CtrlType::kChannel, ma::Camera::CtrlMode::kWrite, value) != MA_OK) {
        MA_LOGE(TAG, "Failed to set camera channel");
        return camera;
    }

    value.u16s[0] = 1920;
    value.u16s[1] = 1080;
    if (camera->commandCtrl(ma::Camera::CtrlType::kWindow, ma::Camera::CtrlMode::kWrite, value) != MA_OK) {
        MA_LOGE(TAG, "Failed to set camera resolution");
        return camera;
    }

    value.i32 = 5;
    camera->commandCtrl(ma::Camera::CtrlType::kFps, ma::Camera::CtrlMode::kWrite, value);

    value.i32 = 0;
    camera->commandCtrl(ma::Camera::CtrlType::kFps, ma::Camera::CtrlMode::kRead, value);
    
    MA_LOGI(MA_TAG, "The value of kFps is: %d",  value.i32);

    value.i32 = 1;
    if (camera->commandCtrl(ma::Camera::CtrlType::kChannel, ma::Camera::CtrlMode::kWrite, value) != MA_OK) {
        MA_LOGE(TAG, "Failed to set camera channel");
        return camera;
    }
    value.u16s[0] = 1920;
    value.u16s[1] = 1080;
    if (camera->commandCtrl(ma::Camera::CtrlType::kWindow, ma::Camera::CtrlMode::kWrite, value) != MA_OK) {
        MA_LOGE(TAG, "Failed to set camera resolution");
        return camera;
    }
    value.i32 = 5;
    camera->commandCtrl(ma::Camera::CtrlType::kFps, ma::Camera::CtrlMode::kWrite, value);

    value.i32 = 0;
    camera->commandCtrl(ma::Camera::CtrlType::kFps, ma::Camera::CtrlMode::kRead, value);
    
    MA_LOGI(MA_TAG, "The value of kFps is: %d",  value.i32);
    value.i32 = 2;
    if (camera->commandCtrl(ma::Camera::CtrlType::kChannel, ma::Camera::CtrlMode::kWrite, value) != MA_OK) {
        MA_LOGE(TAG, "Failed to set camera channel");
        return camera;
    }
    value.i32 = 5;
    camera->commandCtrl(ma::Camera::CtrlType::kFps, ma::Camera::CtrlMode::kWrite, value);

    value.i32 = 0;
    camera->commandCtrl(ma::Camera::CtrlType::kFps, ma::Camera::CtrlMode::kRead, value);
    
    MA_LOGI(MA_TAG, "The value of kFps is: %d",  value.i32);


    camera->startStream(ma::Camera::StreamMode::kRefreshOnReturn);

    return camera;

}

size_t getCurrentRSS() {
    long rss = 0L;
    FILE* fp = nullptr;
    if ((fp = fopen("/proc/self/statm", "r")) == nullptr)
        return 0; // 无法获取
    
    if (fscanf(fp, "%*s%ld", &rss) != 1) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    return rss * sysconf(_SC_PAGESIZE) / 1024; // 转换为 KB
}

ma::Model* initialize_model(const std::string& model_path) noexcept{
    ma::Model* model_null = nullptr;
    ma_err_t ret = MA_OK;
    using namespace ma;
    auto* engine = new ma::engine::EngineCVI();
    ret          = engine->init();
    if (ret != MA_OK) {
        MA_LOGE(TAG, "engine init failed");
        delete engine;
        return model_null;
    }
    ret = engine->load(model_path);

    MA_LOGI(TAG, "engine load model %s", model_path);
    if (ret != MA_OK) {
        MA_LOGE(TAG, "engine load model failed");
        delete engine;
        return model_null;
    }

    ma::Model* model = ma::ModelFactory::create(engine);

    if (model == nullptr) {
        MA_LOGE(TAG, "model not supported");
        ma::ModelFactory::remove(model);
        delete engine;
        return model_null;
    }

    MA_LOGI(TAG, "model type: %d", model->getType());

    return model;
}

void flush_buffer(ma::Camera* camera) {
    ma_img_t tmp_frame;
    while (camera->retrieveFrame(tmp_frame, MA_PIXEL_FORMAT_JPEG) == MA_OK) {
        camera->returnFrame(tmp_frame); // Libera inmediatamente los frames acumulados
    }
}

int extract_number(const std::string& filename) {
    auto dot_pos = filename.find_last_of('.');
    std::string name = (dot_pos == std::string::npos) ? filename : filename.substr(0, dot_pos);
    auto underscore_pos = name.find_last_of('_');
    if (underscore_pos == std::string::npos) return -1;
    std::string num_str = name.substr(underscore_pos + 1);
    if (num_str.empty()) return -1;
    try { return std::stoi(num_str); } catch (...) { return -1; }
}

void cleanup_old_images(const std::string& folder_path, int max_images_to_keep) {
    namespace fs = std::filesystem;
    if (max_images_to_keep <= 0) return;

    try {
        struct Item {
            fs::path path;
            fs::file_time_type t;
            int num;
        };

        std::vector<Item> items;
        for (const auto& entry : fs::directory_iterator(folder_path)) {
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".jpg" || ext == ".jpeg") {
                std::error_code ec;
                auto t = fs::last_write_time(entry, ec);
                if (ec) t = fs::file_time_type::min(); // si falla, lo tratamos como muy antiguo
                items.push_back({entry.path(), t, extract_number(entry.path().filename().string())});
            }
        }

        // Orden: más antiguo primero; si empata tiempo, usar número; si empata, por nombre.
        std::sort(items.begin(), items.end(), [](const Item& a, const Item& b){
            if (a.t != b.t) return a.t < b.t;
            if ((a.num >= 0) && (b.num >= 0) && a.num != b.num) return a.num < b.num;
            return a.path.filename().string() < b.path.filename().string();
        });
        if (items.size() > (size_t)max_images_to_keep) {
            size_t to_delete = items.size() - (size_t)max_images_to_keep;
            for (size_t i = 0; i < to_delete; ++i) {
                std::error_code ec;
                fs::remove(items[i].path, ec);
               
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "cleanup_old_images error: " << e.what() << "\n";
    }
}

int find_max_image_number(const std::string& folder_path) {
    namespace fs = std::filesystem;
    int max_num = 0;
    try {
        for (const auto& entry : fs::directory_iterator(folder_path)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                if (filename.find("alarm_") == 0 && 
                    (filename.find(".jpg") != std::string::npos || 
                     filename.find(".jpeg") != std::string::npos)) {
                    int num = extract_number(filename);
                    if (num > max_num) {
                        max_num = num;
                    }
                }
            }
        }
    } catch (...) {
        // Si hay error al leer el directorio, empezamos desde 0
    }
    return max_num;
}

std::string model_detector_from_mat(ma::Model*& model, cv::Mat& frame,bool report_person_count, bool can_alert_helmet, bool can_zone) {
    auto start = std::chrono::high_resolution_clock::now();
    if (!model) return R"({"error":"model is null"})";
    if (frame.empty()) return R"({"error":"input frame is empty"})";
    
    ma_img_t img;
    img.data   = (uint8_t*)frame.data;
    img.size   = static_cast<uint32_t>(frame.rows * frame.cols * frame.channels());
    img.width  = static_cast<uint32_t>(frame.cols);
    img.height = static_cast<uint32_t>(frame.rows);
    img.format = MA_PIXEL_FORMAT_RGB888;
    img.rotate = MA_PIXEL_ROTATE_0;
    
    auto* detector = static_cast<ma::model::Detector*>(model); 
    detector->run(&img);
    
    auto results = detector->getResults();
     
    bool detected_no_helmet = false;
    bool restricted_zone_violation = false;
    int person_count = 0;
    bool need_output_image = false;
    
    if (can_zone && g_cfg.zone_enabled) {
        cv::Scalar color(g_cfg.zone_b, g_cfg.zone_g, g_cfg.zone_r);

        if (g_cfg.zone_type == "LINE") {
            int div_x = static_cast<int>(g_cfg.zone_div * frame.cols);
            cv::line(frame, {div_x, 0}, {div_x, frame.rows}, color, g_cfg.zone_thick);
            cv::putText(frame, g_cfg.ev_zone, {div_x + 10, 30}, cv::FONT_HERSHEY_SIMPLEX, 0.7, color, 2);

        } else if (g_cfg.zone_type == "LINE_ADVANCED") {
            int sx = static_cast<int>(g_cfg.line_x1 * frame.cols);
            int sy = static_cast<int>(g_cfg.line_y1 * frame.rows);
            int ex = static_cast<int>(g_cfg.line_x2 * frame.cols);
            int ey = static_cast<int>(g_cfg.line_y2 * frame.rows);

            cv::line(frame, {sx, sy}, {ex, ey}, color, g_cfg.zone_thick);
            cv::putText(frame, g_cfg.ev_zone, {(sx + ex) / 2, (sy + ey) / 2}, cv::FONT_HERSHEY_SIMPLEX, 0.7, color, 2);

        } else if (g_cfg.zone_type == "RECTANGLE") {
            int x1 = static_cast<int>(g_cfg.rect_x1 * frame.cols);
            int y1 = static_cast<int>(g_cfg.rect_y1 * frame.rows);
            int x2 = static_cast<int>(g_cfg.rect_x2 * frame.cols);
            int y2 = static_cast<int>(g_cfg.rect_y2 * frame.rows);

            cv::rectangle(frame, {x1, y1}, {x2, y2}, color, g_cfg.zone_thick);
            cv::putText(frame, g_cfg.ev_zone, {x1 + 10, y1 + 30}, cv::FONT_HERSHEY_SIMPLEX, 0.7, color, 2);
        }
    }


    for (const auto& r : results) {
        std::string cls = ClassMapper::get_class(r.target);
        float x1 = (r.x - r.w / 2.0f) * frame.cols;
        float y1 = (r.y - r.h / 2.0f) * frame.rows;
        float x2 = (r.x + r.w / 2.0f) * frame.cols;
        float y2 = (r.y + r.h / 2.0f) * frame.rows;
        cv::Rect rect(cv::Point((int)x1, (int)y1), cv::Point((int)x2, (int)y2));

        if (cls == g_cfg.cls_person) {
            person_count++;

            if (can_zone && g_cfg.zone_enabled) {
                cv::Point person_center(static_cast<int>(r.x * frame.cols),
                                        static_cast<int>(r.y * frame.rows));
                bool in_restricted_zone = false;

                if (g_cfg.zone_type == "LINE") {
                    int divider_x = static_cast<int>(g_cfg.zone_div * frame.cols);
                    in_restricted_zone = (person_center.x > divider_x);

                } else if (g_cfg.zone_type == "LINE_ADVANCED") {
                    int start_x = static_cast<int>(g_cfg.line_x1 * frame.cols);
                    int start_y = static_cast<int>(g_cfg.line_y1 * frame.rows);
                    int end_x   = static_cast<int>(g_cfg.line_x2 * frame.cols);
                    int end_y   = static_cast<int>(g_cfg.line_y2 * frame.rows);

                    cv::Point line_vec(end_x - start_x, end_y - start_y);
                    cv::Point person_vec(person_center.x - start_x, person_center.y - start_y);
                    float cross = line_vec.x * person_vec.y - line_vec.y * person_vec.x;
                    in_restricted_zone = (cross < 0);

                } else if (g_cfg.zone_type == "RECTANGLE") {
                    int rect_x1 = static_cast<int>(g_cfg.rect_x1 * frame.cols);
                    int rect_y1 = static_cast<int>(g_cfg.rect_y1 * frame.rows);
                    int rect_x2 = static_cast<int>(g_cfg.rect_x2 * frame.cols);
                    int rect_y2 = static_cast<int>(g_cfg.rect_y2 * frame.rows);

                    cv::Rect zone_rect(rect_x1, rect_y1, rect_x2 - rect_x1, rect_y2 - rect_y1);
                    in_restricted_zone = zone_rect.contains(person_center);
                }

                if (in_restricted_zone) {
                    restricted_zone_violation = true;
                    need_output_image = true;

                    cv::rectangle(frame, rect, cv::Scalar(0, 0, 255), 3);
                    cv::putText(frame, "ZONE ALERT",
                                {std::max(0, (int)x1), std::max(15, (int)y1 - 10)},
                                cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 0, 255), 2);
                }
            }
        }

        if (cls == g_cfg.cls_no_helmet && can_alert_helmet) {
            detected_no_helmet = true;
            need_output_image = true;

            cv::Scalar color = ColorPalette::getColor(r.target);
            cv::rectangle(frame, rect, color, 3);
            cv::putText(frame, g_cfg.ev_no_helmet,
                        {std::max(0, (int)x1), std::max(15, (int)y1 - 10)},
                        cv::FONT_HERSHEY_SIMPLEX, 0.6, color, 2);
        }
    }

    json result_json;
    if (report_person_count) result_json["person_count"] = person_count;
    if (detected_no_helmet) result_json[g_cfg.ev_no_helmet] = true;
    if (restricted_zone_violation) result_json[g_cfg.ev_zone] = true;
    result_json["cooldown"] = false;

    if (need_output_image) {
        namespace fs = std::filesystem;
        std::string folder_name = g_cfg.dir_images;
        std::string folder_name_bak = g_cfg.dir_images_bak;
        
        std::string target_folder = folder_name;
        bool use_backup = false;
        bool can_save_image = true;
        // Primero intentamos con la carpeta principal
        try {
            if (!fs::exists(folder_name)) {
                // Si no existe, intentamos crear la carpeta principal
                fs::create_directory(folder_name);
            }
            // Verificamos que realmente podemos acceder a la carpeta
            if (!fs::is_directory(folder_name)) {
                throw std::runtime_error("No es un directorio válido");
            }
        } catch (...) {
            // Si falla la creación o acceso a la carpeta principal, usamos el backup
            use_backup = true;
            target_folder = folder_name_bak;
            
            try {
                // Intentamos crear la carpeta de respaldo si no existe
                if (!fs::exists(folder_name_bak)) {
                    fs::create_directory(folder_name_bak);
                }
            } catch (...) {
                // Si también falla la creación del backup, no guardamos la imagen
                std::cout << "Error: No se pudo acceder a ninguna carpeta de imágenes" << std::endl;
                can_save_image = false;
            }
        }
        


       if (can_save_image) {
            try {
                static std::mutex counter_mutex;
                static int image_counter = find_max_image_number(target_folder);
                
                std::lock_guard<std::mutex> lock(counter_mutex);
                image_counter++;

                std::string filename = target_folder + "/alarm_" + std::to_string(image_counter) + ".jpg";
                cv::Mat image_bgr;
                if (frame.channels() == 3) {
                    cv::cvtColor(frame, image_bgr, cv::COLOR_RGB2BGR);
                } else {
                    image_bgr = frame.clone();
                }

                if (cv::imwrite(filename, image_bgr)) {
                    cleanup_old_images(target_folder, g_cfg.max_images);
                    
                    // Opcional: mostrar en qué carpeta se guardó
                    if (use_backup) {
                        std::cout << "Imagen guardada en respaldo: " << filename << std::endl;
                    } else {
                        std::cout << "Imagen guardada en principal: " << filename << std::endl;
                    }
                }
            } catch (...) {
                std::cout << "Error al guardar la imagen" << std::endl;
            }
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    return result_json.dump();
}