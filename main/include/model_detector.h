#ifndef MODEL_DETECTOR_H
#define MODEL_DETECTOR_H
// 头文件内容
#endif

#include <opencv2/opencv.hpp>
#include <sscma.h>
#include <numeric>
#include <queue>
#include <mutex>
#include <condition_variable>

extern std::mutex save_mutex;
extern std::condition_variable save_cv;
extern bool stop_saver;

struct SaveTask {
    cv::Mat image;
    std::string path;
    bool is_training = false; 
};




// 图像预处理函数声明
cv::Mat preprocessImage(cv::Mat& image, ma::Model* model);

void release_camera(ma::Camera*& camera) noexcept;
void release_model(ma::Model*& model, ma::engine::EngineCVI*& engine);
size_t getCurrentRSS();
// 初始化函数
ma::Camera* initialize_camera() noexcept;
ma::Model* initialize_model(const std::string& model_path) noexcept;
void imageSaverThread();
// 目标检测主函数
//std::string model_detector(ma::Model*& model, ma::Camera*& camera,int& i);
// En el archivo donde está model_detector (declaración en .h)
extern std::queue<SaveTask> save_queue;
std::string model_detector(
    ma::Model*& model,
    cv::Mat& input_bgr_or_rgb888,
    bool person_count,
    bool can_alert,
    bool can_zone
);