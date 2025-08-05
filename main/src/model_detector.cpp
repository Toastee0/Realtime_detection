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

std::string model_detector(ma::Model*& model, ma::Camera*& camera, int& i, bool report_person_count, bool can_alert_helmet,  bool can_zone) {
    printf("Model detector started\n");
    
    cv::Mat image_catch, image;
    ma_img_t jpeg;
    json result_json;

    // Parameters for restricted zone detection
    bool check_restricted_zone = true; // Enable/disable zone checking
    float zone_divider = 0.5f; // 50% of image width as divider

    // 1. Efficient buffer cleanup
    flush_buffer(camera);

    // 2. Capture most recent frame
    int retry_count = 0;
    ma_err_t ret;
    do {
        ret = camera->retrieveFrame(jpeg, MA_PIXEL_FORMAT_JPEG);
        if (ret != MA_OK) {
            retry_count++;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    } while (ret != MA_OK && retry_count < 3);

    if (ret != MA_OK) {
        MA_LOGE(TAG, "Failed to capture frame after %d attempts", retry_count);
        result_json["error"] = "Camera capture failed";
        return result_json.dump();
    }

    // 3. Process frame
    image_catch = cv::imdecode(cv::Mat(1, jpeg.size, CV_8UC1, jpeg.data), cv::IMREAD_COLOR);
    camera->returnFrame(jpeg);

    if (image_catch.empty()) {
        result_json["error"] = "read image failed";
        return result_json.dump();
    }

    image = preprocessImage(image_catch, model);
    if (image.empty()) {  
        result_json["error"] = "preprocessed image is empty";
        return result_json.dump();
    }

    // 4. Prepare for inference
    ma_img_t img;
    img.data = (uint8_t*)image.data;
    img.size = image.rows * image.cols * image.channels();
    img.width = image.cols;
    img.height = image.rows;
    img.format = MA_PIXEL_FORMAT_RGB888;
    img.rotate = MA_PIXEL_ROTATE_0;

    // 5. Run detection
    ma::model::Detector* detector = static_cast<ma::model::Detector*>(model);
    detector->run(&img);

    auto _results = detector->getResults();
    bool detected_no_helmet = false;
    bool restricted_zone_violation = false;
    cv::Mat output_image;
    int person_count = 0;
    bool cooldown = false;

    // First pass: Check for violations and prepare output image if needed
    bool need_output_image = false;
    for (auto result : _results) {
        std::string class_name = ClassMapper::get_class(result.target);
        
        if (class_name == "person" && check_restricted_zone && result.x > zone_divider && can_zone) {
            restricted_zone_violation = true;
            need_output_image = true;
        }
        
        if (class_name == "no_helmet" && can_alert_helmet) {
            detected_no_helmet = true;
            need_output_image = true;
        }
    }

    // Second pass: Draw annotations if needed
    if (need_output_image) {
        image.copyTo(output_image);
        cv::cvtColor(output_image, output_image, cv::COLOR_RGB2BGR);
        
        // Draw zone divider if checking restricted zones
        if (check_restricted_zone && can_zone) {
            int divider_x = static_cast<int>(zone_divider * image.cols);
            cv::line(output_image, cv::Point(divider_x, 0), 
                    cv::Point(divider_x, image.rows),
                    cv::Scalar(0, 0, 255), 2);
        }

        for (auto result : _results) {
            std::string class_name = ClassMapper::get_class(result.target);
            float x1 = (result.x - result.w / 2.0) * image.cols;
            float y1 = (result.y - result.h / 2.0) * image.rows;
            float x2 = (result.x + result.w / 2.0) * image.cols;
            float y2 = (result.y + result.h / 2.0) * image.rows;

            if (class_name == "person") {
                person_count++;
                
                if (check_restricted_zone && result.x > zone_divider && can_zone) {
                    cv::rectangle(output_image, cv::Point(x1, y1), cv::Point(x2, y2), 
                                cv::Scalar(0, 0, 255), 3);
                    cv::putText(output_image, "RESTRICTED ZONE", cv::Point(x1, y1 - 10),
                              cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 0, 255), 2);
                }
            }
            
            if (class_name == "no_helmet" && can_alert_helmet) {
                cv::rectangle(output_image, cv::Point(x1, y1), cv::Point(x2, y2), 
                            ColorPalette::getColor(result.target), 3);
                cv::putText(output_image, class_name, cv::Point(x1, y1 - 10),
                          cv::FONT_HERSHEY_SIMPLEX, 0.6, 
                          ColorPalette::getColor(result.target), 2);
            }
        }
    } else {
        // Just count persons if no output image needed
        for (auto result : _results) {
            if (ClassMapper::get_class(result.target) == "person") {
                person_count++;
            }
        }
    }

    // Prepare JSON result
    if (report_person_count) {
        result_json["person_count"] = person_count;
    }
    
    if (cooldown) {
        result_json["cooldown"] = true;
    }
    
    if (detected_no_helmet) {
        result_json["detected_no_helmet"] = true;
    }
    
    if (restricted_zone_violation) {
        result_json["restricted_zone_violation"] = true;
    }
    
    // Save image if there were any violations
    if (need_output_image) {
        std::string filename = "image_detected_" + std::to_string(i) + ".jpg";
        try {
            if (cv::imwrite(filename, output_image)) {
                result_json["image_saved"] = filename;
            } else {
                MA_LOGE(TAG, "Error saving image: %s", filename.c_str());
            }
        } catch (const cv::Exception& e) {
            MA_LOGE(TAG, "OpenCV error: %s", e.what());
        }
    }

    return result_json.dump();

}