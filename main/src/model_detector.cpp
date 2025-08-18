#include <chrono>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdio.h>
#include <string>
#include <opencv2/opencv.hpp>
#include <sscma.h>
#include <vector>
#include <thread>       // 解决std::this_thread错误
#include <iterator>

#include <ClassMapper.h>   
#include <unistd.h>
#include <filesystem> 
#include <nlohmann/json.hpp>
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
// 定义函数（放在 main 之前）
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


std::string model_detector_from_mat(ma::Model*& model,cv::Mat& input_bgr_or_rgb888,int frame_id,bool report_person_count,bool can_alert_helmet,bool can_zone) {
    auto start = std::chrono::high_resolution_clock::now();  // tiempo inicialInferencia =====
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
  
    // ===== Flags y resultados =====
    bool detected_no_helmet = false;
    bool restricted_zone_violation = false;
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

        if (cls == "person") {
            person_count++;
            if (can_zone && r.x > zone_divider) {
                restricted_zone_violation = true;
                need_output_image = true;

                cv::rectangle(input_bgr_or_rgb888, rect, cv::Scalar(0, 0, 255), 3);
                cv::putText(input_bgr_or_rgb888, "RESTRICTED ZONE",
                            {std::max(0, (int)x1), std::max(15, (int)y1 - 10)},
                            cv::FONT_HERSHEY_SIMPLEX, 0.6, {0, 0, 255}, 2);
            }
        }

        if (cls == "no_helmet" && can_alert_helmet) {
            detected_no_helmet = true;
            need_output_image = true;

            // 🟡 Dibujar alerta de casco
            cv::Scalar color = ColorPalette::getColor(r.target);
            cv::rectangle(input_bgr_or_rgb888, rect, color, 3);
            cv::putText(input_bgr_or_rgb888, "NO_HELMET",
                        {std::max(0, (int)x1), std::max(15, (int)y1 - 10)},
                        cv::FONT_HERSHEY_SIMPLEX, 0.6, color, 2);
        }
    }

    
    if (can_zone) {
        int divider_x = static_cast<int>(zone_divider * input_bgr_or_rgb888.cols);
        cv::line(input_bgr_or_rgb888, {divider_x, 0}, {divider_x, input_bgr_or_rgb888.rows}, {0, 0, 255}, 2);
    }

    json result_json;
    if (report_person_count) result_json["person_count"] = person_count;
    if (detected_no_helmet) result_json["detected_no_helmet"] = true;
    if (restricted_zone_violation) result_json["restricted_zone_violation"] = true;
    result_json["cooldown"] = false;
    
    if (need_output_image) {
        namespace fs = std::filesystem;
        std::string folder_name = "images";
        if (!fs::exists(folder_name)) {
            try { fs::create_directory(folder_name); } catch (...) {}
        }
        std::string filename = folder_name + "/alarm_" + std::to_string(frame_id) + ".jpg";
        if (cv::imwrite(filename, input_bgr_or_rgb888)) {
            result_json["image_saved"] = filename;
        }
    }
    auto end = std::chrono::high_resolution_clock::now();  // tiempo final
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << "Tiempotiempo : " << duration << " ms" << std::endl;
    return result_json.dump();
}
