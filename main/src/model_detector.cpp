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

#include <nlohmann/json.hpp>
using json = nlohmann::json;



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

std::string model_detector(ma::Model*& model, ma::Camera*& camera, int& i, bool report_person_count, bool can_alert_helmet, bool can_zone) {
    ma_img_t jpeg;
    const int max_retries = 3;  // Número máximo de reintentos
    int retry_count = 0;
    bool frame_retrieved = false;

    size_t mem_init = getCurrentRSS();
    MA_LOGI(MA_TAG, "Memory usage: %d KB", mem_init);

    // Intento de recuperación de marco con reintentos
    while (retry_count < max_retries) {
        camera->retrieveFrame(jpeg, MA_PIXEL_FORMAT_JPEG);
        MA_LOGI(MA_TAG, "jpeg size: %d", jpeg.size);

        if (jpeg.size == 0 || jpeg.data == nullptr) {
            MA_LOGW(TAG, "Failed to retrieve frame (attempt %d of %d)", retry_count+1, max_retries);
            retry_count++;
            
            // Retroceso de 100 ms antes de reintentar
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        } else {
            frame_retrieved = true;
            break;
        }
    }

    if (!frame_retrieved) {
        MA_LOGE(TAG, "jpeg frame is empty after %d attempts", max_retries);
        return "{\"error\":\"jpeg frame is empty\"}";
    }

    cv::Mat image_catch = cv::imdecode(cv::Mat(1, jpeg.size, CV_8UC1, jpeg.data), cv::IMREAD_COLOR);
    camera->returnFrame(jpeg);
    
    size_t mem_post_capture = getCurrentRSS();
    MA_LOGI(MA_TAG, "Memory usage_after: %d KB", mem_post_capture);

    if (image_catch.empty()) {
        MA_LOGE(TAG, "read image failed");
        return "{\"error\":\"read image failed\"}";
    }

    cv::Mat image = preprocessImage(image_catch, model);
    if (image.empty()) {
        MA_LOGE(TAG, "preprocessed image is empty");
        return "{\"error\":\"preprocessed image is empty\"}";
    }

    ma_img_t img;
    img.data   = (uint8_t*)image.data;
    img.size   = static_cast<uint32_t>(image.rows * image.cols * image.channels());
    img.width  = static_cast<uint32_t>(image.cols);
    img.height = static_cast<uint32_t>(image.rows);
    img.format = MA_PIXEL_FORMAT_RGB888;
    img.rotate = MA_PIXEL_ROTATE_0;

    size_t mem_pre_model = getCurrentRSS();
    MA_LOGI(MA_TAG, "Memory usage_before_model: %d KB", mem_pre_model);

    ma::model::Detector* detector = static_cast<ma::model::Detector*>(model);
    detector->run(&img);
    auto _results = detector->getResults();

    bool detected_no_helmet = false;
    bool restricted_zone_violation = false;
    int person_count = 0;
    const bool check_restricted_zone = true;
    const float zone_divider = 0.5f;
    bool need_output_image = false;

    // Primera pasada: verificar violaciones
    for (const auto& result : _results) {
        const std::string class_name = ClassMapper::get_class(result.target);
        
        if (class_name == "person") {
            person_count++;
            if (check_restricted_zone && result.x > zone_divider && can_zone) {
                restricted_zone_violation = true;
                need_output_image = true;
            }
        }
        else if (class_name == "no_helmet" && can_alert_helmet) {
            detected_no_helmet = true;
            need_output_image = true;
        }
    }

    cv::Mat output_image;
    if (need_output_image) {
        cv::cvtColor(image, output_image, cv::COLOR_RGB2BGR);
        
        if (check_restricted_zone && can_zone) {
            int divider_x = static_cast<int>(zone_divider * output_image.cols);
            cv::line(output_image, cv::Point(divider_x, 0), 
                    cv::Point(divider_x, output_image.rows),
                    cv::Scalar(0, 0, 255), 2);
        }

        // Segunda pasada: dibujar anotaciones
        for (const auto& result : _results) {
            const std::string class_name = ClassMapper::get_class(result.target);
            const float x1 = (result.x - result.w / 2.0f) * output_image.cols;
            const float y1 = (result.y - result.h / 2.0f) * output_image.rows;
            const float x2 = (result.x + result.w / 2.0f) * output_image.cols;
            const float y2 = (result.y + result.h / 2.0f) * output_image.rows;
            const cv::Rect rect(x1, y1, x2 - x1, y2 - y1);

            if (class_name == "person" && check_restricted_zone && result.x > zone_divider && can_zone) {
                cv::rectangle(output_image, rect, cv::Scalar(0, 0, 255), 3);
                cv::putText(output_image, "RESTRICTED ZONE", cv::Point(x1, y1 - 10),
                          cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 0, 255), 2);
            }
            else if (class_name == "no_helmet" && can_alert_helmet) {
                const cv::Scalar color = ColorPalette::getColor(result.target);
                cv::rectangle(output_image, rect, color, 3);
                cv::putText(output_image, "no_helmet", cv::Point(x1, y1 - 10),
                          cv::FONT_HERSHEY_SIMPLEX, 0.6, color, 2);
            }
        }
    }

    json result_json;
    if (report_person_count) result_json["person_count"] = person_count;
    if (detected_no_helmet) result_json["detected_no_helmet"] = true;
    if (restricted_zone_violation) result_json["restricted_zone_violation"] = true;
    result_json["cooldown"] = false;

    if (need_output_image) {
        std::string filename = "image_detected_" + std::to_string(i) + ".jpg";
        if (cv::imwrite(filename, output_image)) {
            result_json["image_saved"] = filename;
        } else {
            MA_LOGE(TAG, "Error saving image: %s", filename.c_str());
        }
    }

    return result_json.dump();
}