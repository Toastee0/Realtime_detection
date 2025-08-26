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

std::string model_detector_from_mat(ma::Model*& model, cv::Mat& input_bgr_or_rgb888, int frame_id, bool report_person_count, bool can_alert_helmet, bool can_zone) {
    auto start = std::chrono::high_resolution_clock::now();
    if (!model) return R"({"error":"model is null"})";
    if (input_bgr_or_rgb888.empty()) return R"({"error":"input frame is empty"})";
    
    ma_img_t img;
    img.data   = (uint8_t*)input_bgr_or_rgb888.data;
    img.size   = static_cast<uint32_t>(input_bgr_or_rgb888.rows * input_bgr_or_rgb888.cols * input_bgr_or_rgb888.channels());
    img.width  = static_cast<uint32_t>(input_bgr_or_rgb888.cols);
    img.height = static_cast<uint32_t>(input_bgr_or_rgb888.rows);
    img.format = MA_PIXEL_FORMAT_RGB888;
    img.rotate = MA_PIXEL_ROTATE_0;
    
    auto* detector = static_cast<ma::model::Detector*>(model); 
    detector->run(&img);
    
    auto results = detector->getResults();
  
    bool detected_no_helmet = false;
    bool restricted_zone = false;
    int person_count = 0;
    const float zone_divider = 0.5f;
    bool need_output_image = false;
    
    for (const auto& r : results) {
        std::string cls = ClassMapper::get_class(r.target);
        float x1 = (r.x - r.w / 2.0f) * input_bgr_or_rgb888.cols;
        float y1 = (r.y - r.h / 2.0f) * input_bgr_or_rgb888.rows;
        float x2 = (r.x + r.w / 2.0f) * input_bgr_or_rgb888.cols;
        float y2 = (r.y + r.h / 2.0f) * input_bgr_or_rgb888.rows;
        cv::Rect rect(cv::Point((int)x1, (int)y1), cv::Point((int)x2, (int)y2));

        if (cls == DETECTION_CLASS_PERSON) {
            person_count++;
            if (can_zone && r.x > zone_divider) {
                restricted_zone = true;
                need_output_image = true;

                cv::rectangle(input_bgr_or_rgb888, rect, VISUAL_ZONE_DIVIDER_COLOR, VISUAL_BBOX_THICKNESS);
                cv::putText(input_bgr_or_rgb888, EVENT_RESTRICTED_ZONE,
                            {std::max(0, (int)x1), std::max(15, (int)y1 - 10)},
                            cv::FONT_HERSHEY_SIMPLEX, 0.6, VISUAL_ZONE_DIVIDER_COLOR, 2);
            }
        }

        if (cls == DETECTION_CLASS_NO_HELMET && can_alert_helmet) {
            detected_no_helmet = true;
            need_output_image = true;

            cv::Scalar color = ColorPalette::getColor(r.target);
            cv::rectangle(input_bgr_or_rgb888, rect, color, VISUAL_ZONE_DIVIDER_THICKNESS);
            cv::putText(input_bgr_or_rgb888, DETECTION_CLASS_NO_HELMET,
                        {std::max(0, (int)x1), std::max(15, (int)y1 - 10)},
                        cv::FONT_HERSHEY_SIMPLEX, 0.6, color, 2);
        }
    }
    
    if (can_zone) {
        int divider_x = static_cast<int>(zone_divider * input_bgr_or_rgb888.cols);
        cv::line(input_bgr_or_rgb888,
                cv::Point(ZONE_LINE_X1, ZONE_LINE_Y1),
                cv::Point(ZONE_LINE_X2, ZONE_LINE_Y2),
                VISUAL_ZONE_DIVIDER_COLOR,
                VISUAL_ZONE_DIVIDER_THICKNESS);

    }

    json result_json;
    if (report_person_count) result_json["person_count"] = person_count;
    if (detected_no_helmet) result_json[EVENT_DETECTED_NO_HELMET] = true;
    if (restricted_zone) result_json[EVENT_RESTRICTED_ZONE] = true;
    result_json["cooldown"] = false;
    
    if (need_output_image) {
    namespace fs = std::filesystem;
    std::string folder_name = PATH_IMAGES_DIR;  // ← Usando la constante global
    
    if (!fs::exists(folder_name)) {
        try { 
            fs::create_directory(folder_name); 
        } catch (...) {
            // Opcional: Loggear error de creación de directorio
        }
    }
    // Limpieza automática (opcional) generar funcion
    // cleanup_old_images(folder_name, MAX_IMAGES_STORED, MAX_STORAGE_SIZE_MB);
    
    
    std::string filename = folder_name + "/alarm_" + std::to_string(frame_id) + ".jpg";
    if (cv::imwrite(filename, input_bgr_or_rgb888)) {
        result_json["image_saved"] = filename;
    }
}
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    return result_json.dump();
}