#include "esp32_camera.h"
#include "mcp_server.h"
#include "display.h"
#include "board.h"
#include "system_info.h"

#include <cJSON.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <img_converters.h>
#include <mbedtls/base64.h>
#include <cstdlib>
#include <cstring>

#define TAG "Esp32Camera"

namespace {

bool EncodeFrameToBase64(camera_fb_t* frame, std::string& base64_image, std::string& error_message) {
    if (frame == nullptr) {
        error_message = "No image captured";
        return false;
    }

    uint8_t* jpeg_buffer = nullptr;
    size_t jpeg_length = 0;
    bool own_jpeg_buffer = false;

    if (frame->format == PIXFORMAT_JPEG) {
        jpeg_buffer = frame->buf;
        jpeg_length = frame->len;
    } else {
        if (!frame2jpg(frame, 80, &jpeg_buffer, &jpeg_length) || jpeg_buffer == nullptr || jpeg_length == 0) {
            ESP_LOGE(TAG, "Failed to encode frame as JPEG");
            error_message = "Failed to encode JPEG";
            return false;
        }
        own_jpeg_buffer = true;
    }

    size_t base64_length = 0;
    mbedtls_base64_encode(nullptr, 0, &base64_length, jpeg_buffer, jpeg_length);

    auto* base64_buffer = static_cast<uint8_t*>(
        heap_caps_malloc(base64_length + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
    );
    if (base64_buffer == nullptr) {
        if (own_jpeg_buffer) {
            free(jpeg_buffer);
        }
        ESP_LOGE(TAG, "Failed to allocate base64 buffer");
        error_message = "Memory allocation failed";
        return false;
    }

    if (mbedtls_base64_encode(base64_buffer, base64_length + 1, &base64_length, jpeg_buffer, jpeg_length) != 0) {
        heap_caps_free(base64_buffer);
        if (own_jpeg_buffer) {
            free(jpeg_buffer);
        }
        ESP_LOGE(TAG, "Base64 encoding failed");
        error_message = "Base64 encoding failed";
        return false;
    }
    base64_buffer[base64_length] = '\0';

    if (own_jpeg_buffer) {
        free(jpeg_buffer);
    }

    base64_image.assign(reinterpret_cast<const char*>(base64_buffer), base64_length);
    heap_caps_free(base64_buffer);
    return true;
}

std::string BuildJsonPayload(const std::string& image_base64, const char* name = nullptr) {
    cJSON* payload = cJSON_CreateObject();
    if (payload == nullptr) {
        return {};
    }

    cJSON_AddStringToObject(payload, "image_base64", image_base64.c_str());
    if (name != nullptr) {
        cJSON_AddStringToObject(payload, "name", name);
    } else {
        cJSON_AddNumberToObject(payload, "confidence_threshold", 0.45);
    }

    char* json_str = cJSON_PrintUnformatted(payload);
    std::string result;
    if (json_str != nullptr) {
        result = json_str;
        cJSON_free(json_str);
    }
    cJSON_Delete(payload);
    return result;
}

std::string BuildLocatePayload(const std::string& image_base64, const char* name, double confidence_threshold) {
    cJSON* payload = cJSON_CreateObject();
    if (payload == nullptr) {
        return {};
    }

    cJSON_AddStringToObject(payload, "image_base64", image_base64.c_str());
    cJSON_AddStringToObject(payload, "name", name);
    cJSON_AddNumberToObject(payload, "confidence_threshold", confidence_threshold);

    char* json_str = cJSON_PrintUnformatted(payload);
    std::string result;
    if (json_str != nullptr) {
        result = json_str;
        cJSON_free(json_str);
    }
    cJSON_Delete(payload);
    return result;
}

std::string ExtractApiErrorMessage(const std::string& response_body, int status_code) {
    cJSON* response = cJSON_Parse(response_body.c_str());
    if (response != nullptr) {
        cJSON* detail = cJSON_GetObjectItem(response, "detail");
        cJSON* error = cJSON_GetObjectItem(response, "error");
        if (cJSON_IsString(detail) && detail->valuestring != nullptr) {
            std::string message = detail->valuestring;
            cJSON_Delete(response);
            return message;
        }
        if (cJSON_IsString(error) && error->valuestring != nullptr) {
            std::string message = error->valuestring;
            cJSON_Delete(response);
            return message;
        }
        cJSON_Delete(response);
    }

    char fallback[64];
    snprintf(fallback, sizeof(fallback), "API error: %d", status_code);
    return std::string(fallback);
}

}  // namespace

Esp32Camera::Esp32Camera(const camera_config_t& config) {
    // camera init
    esp_err_t err = esp_camera_init(&config); // 配置上面定义的参数
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
        return;
    }

    sensor_t *s = esp_camera_sensor_get(); // 获取摄像头型号
    if (s->id.PID == GC0308_PID) {
        s->set_hmirror(s, 0);  // 这里控制摄像头镜像 写1镜像 写0不镜像
    }

    // 初始化预览图片的内存
    memset(&preview_image_, 0, sizeof(preview_image_));
    preview_image_.header.magic = LV_IMAGE_HEADER_MAGIC;
    preview_image_.header.cf = LV_COLOR_FORMAT_RGB565;
    preview_image_.header.flags = LV_IMAGE_FLAGS_ALLOCATED | LV_IMAGE_FLAGS_MODIFIABLE;

    switch (config.frame_size) {
        case FRAMESIZE_SVGA:
            preview_image_.header.w = 800;
            preview_image_.header.h = 600;
            break;
        case FRAMESIZE_VGA:
            preview_image_.header.w = 640;
            preview_image_.header.h = 480;
            break;
        case FRAMESIZE_QVGA:
            preview_image_.header.w = 320;
            preview_image_.header.h = 240;
            break;
        case FRAMESIZE_128X128:
            preview_image_.header.w = 128;
            preview_image_.header.h = 128;
            break;
        case FRAMESIZE_240X240:
            preview_image_.header.w = 240;
            preview_image_.header.h = 240;
            break;
        default:
            ESP_LOGE(TAG, "Unsupported frame size: %d, image preview will not be shown", config.frame_size);
            preview_image_.data_size = 0;
            preview_image_.data = nullptr;
            return;
    }

    preview_image_.header.stride = preview_image_.header.w * 2;
    preview_image_.data_size = preview_image_.header.w * preview_image_.header.h * 2;
    preview_image_.data = (uint8_t*)heap_caps_malloc(preview_image_.data_size, MALLOC_CAP_SPIRAM);
    if (preview_image_.data == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate memory for preview image");
        return;
    }
}

Esp32Camera::~Esp32Camera() {
    if (fb_) {
        esp_camera_fb_return(fb_);
        fb_ = nullptr;
    }
    if (preview_image_.data) {
        heap_caps_free((void*)preview_image_.data);
        preview_image_.data = nullptr;
    }
    esp_camera_deinit();
}

void Esp32Camera::SetExplainUrl(const std::string& url, const std::string& token) {
    explain_url_ = url;
    explain_token_ = token;
}

void Esp32Camera::SetFaceEnrollUrl(const std::string& url) {
    face_enroll_url_ = url;
}

void Esp32Camera::SetFaceTrackUrl(const std::string& url) {
    face_track_url_ = url;
}

bool Esp32Camera::Capture() {
    if (encoder_thread_.joinable()) {
        encoder_thread_.join();
    }

    int frames_to_get = 2;
    // Try to get a stable frame
    for (int i = 0; i < frames_to_get; i++) {
        if (fb_ != nullptr) {
            esp_camera_fb_return(fb_);
        }
        fb_ = esp_camera_fb_get();
        if (fb_ == nullptr) {
            ESP_LOGE(TAG, "Camera capture failed");
            return false;
        }
    }

    // 如果预览图片 buffer 为空，则跳过预览
    // 但仍返回 true，因为此时图像可以上传至服务器
    if (preview_image_.data_size == 0) {
        ESP_LOGW(TAG, "Skip preview because of unsupported frame size");
        return true;
    }
    if (preview_image_.data == nullptr) {
        ESP_LOGE(TAG, "Preview image data is not initialized");
        return true;
    }
    // 显示预览图片
    auto display = Board::GetInstance().GetDisplay();
    if (display != nullptr) {
        auto src = (uint16_t*)fb_->buf;
        auto dst = (uint16_t*)preview_image_.data;
        size_t pixel_count = fb_->len / 2;
        for (size_t i = 0; i < pixel_count; i++) {
            // 交换每个16位字内的字节
            dst[i] = __builtin_bswap16(src[i]);
        }
        display->SetPreviewImage(&preview_image_);
    }
    return true;
}

std::string Esp32Camera::RecognizeFace(const std::string& url) {
    if (url.empty()) {
        return "<rec>Face recognition URL not configured</rec>";
    }
    std::string base64_image;
    std::string error_message;
    if (!EncodeFrameToBase64(fb_, base64_image, error_message)) {
        return std::string("<rec>") + error_message + "</rec>";
    }

    std::string payload = BuildJsonPayload(base64_image);
    if (payload.empty()) {
        return "<rec>Failed to build request</rec>";
    }

    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(3);
    http->SetHeader("Content-Type", "application/json");
    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    http->SetHeader("Client-Id", Board::GetInstance().GetUuid().c_str());
    http->SetContent(std::move(payload));

    ESP_LOGI(TAG, "Sending face recognition request to %s", url.c_str());
    if (!http->Open("POST", url)) {
        ESP_LOGE(TAG, "Failed to connect to face recognition API");
        return "<rec>Failed to connect to API</rec>";
    }

    int status_code = http->GetStatusCode();
    if (status_code != 200) {
        ESP_LOGE(TAG, "Face recognition API returned status code %d", status_code);
        http->Close();
        char error_output[64];
        snprintf(error_output, sizeof(error_output), "<rec>API error: %d</rec>", status_code);
        return std::string(error_output);
    }

    std::string response_string = http->ReadAll();
    http->Close();

    cJSON* response = cJSON_Parse(response_string.c_str());
    if (response == nullptr) {
        ESP_LOGE(TAG, "Invalid face recognition response: %s", response_string.c_str());
        return "<rec>Invalid API response</rec>";
    }

    std::string result = "<rec>Invalid API response</rec>";
    cJSON* matched = cJSON_GetObjectItem(response, "matched");
    cJSON* name = cJSON_GetObjectItem(response, "name");
    cJSON* confidence = cJSON_GetObjectItem(response, "confidence");

    if (cJSON_IsBool(matched) && cJSON_IsNumber(confidence)) {
        char output[160];
        if (cJSON_IsTrue(matched) && cJSON_IsString(name)) {
            snprintf(output, sizeof(output), "<rec>%s, confidence: %.2f</rec>",
                     name->valuestring, confidence->valuedouble);
        } else {
            snprintf(output, sizeof(output), "<rec>No match, confidence: %.2f</rec>",
                     confidence->valuedouble);
        }
        result = output;
    }

    cJSON_Delete(response);
    return result;
}

std::string Esp32Camera::EnrollFace(const std::string& url, const std::string& name) {
    if (url.empty()) {
        return "<enroll>Face enrollment URL not configured</enroll>";
    }
    if (name.empty()) {
        return "<enroll>Name is required</enroll>";
    }

    std::string base64_image;
    std::string error_message;
    if (!EncodeFrameToBase64(fb_, base64_image, error_message)) {
        return std::string("<enroll>") + error_message + "</enroll>";
    }

    std::string payload = BuildJsonPayload(base64_image, name.c_str());
    if (payload.empty()) {
        return "<enroll>Failed to build request</enroll>";
    }

    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(3);
    http->SetHeader("Content-Type", "application/json");
    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    http->SetHeader("Client-Id", Board::GetInstance().GetUuid().c_str());
    http->SetContent(std::move(payload));

    ESP_LOGI(TAG, "Sending face enrollment request to %s for %s", url.c_str(), name.c_str());
    if (!http->Open("POST", url)) {
        ESP_LOGE(TAG, "Failed to connect to face enrollment API");
        return "<enroll>Failed to connect to API</enroll>";
    }

    int status_code = http->GetStatusCode();
    std::string response_string = http->ReadAll();
    http->Close();

    if (status_code != 200) {
        std::string api_error = ExtractApiErrorMessage(response_string, status_code);
        ESP_LOGE(TAG, "Face enrollment API returned status code %d: %s", status_code, api_error.c_str());
        return std::string("<enroll>failed: ") + api_error + "</enroll>";
    }

    cJSON* response = cJSON_Parse(response_string.c_str());
    if (response == nullptr) {
        ESP_LOGE(TAG, "Invalid face enrollment response: %s", response_string.c_str());
        return "<enroll>Invalid API response</enroll>";
    }

    std::string result = "<enroll>Invalid API response</enroll>";
    cJSON* success = cJSON_GetObjectItem(response, "success");
    cJSON* response_name = cJSON_GetObjectItem(response, "name");
    cJSON* error = cJSON_GetObjectItem(response, "error");

    if (cJSON_IsBool(success)) {
        if (cJSON_IsTrue(success) && cJSON_IsString(response_name) && response_name->valuestring != nullptr) {
            result = std::string("<enroll>registered: ") + response_name->valuestring + "</enroll>";
        } else if (cJSON_IsString(error) && error->valuestring != nullptr) {
            result = std::string("<enroll>failed: ") + error->valuestring + "</enroll>";
        }
    }

    cJSON_Delete(response);
    return result;
}

std::string Esp32Camera::EnrollPerson(const std::string& name) {
    return EnrollFace(face_enroll_url_, name);
}

std::string Esp32Camera::LocatePerson(const std::string& name) {
    if (face_track_url_.empty()) {
        return "{\"success\":false,\"matched\":false,\"error\":\"Face locate URL not configured\"}";
    }
    if (name.empty()) {
        return "{\"success\":false,\"matched\":false,\"error\":\"Name is required\"}";
    }

    std::string base64_image;
    std::string error_message;
    if (!EncodeFrameToBase64(fb_, base64_image, error_message)) {
        cJSON* response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddBoolToObject(response, "matched", false);
        cJSON_AddStringToObject(response, "error", error_message.c_str());
        char* json_str = cJSON_PrintUnformatted(response);
        std::string result = json_str ? json_str : "{\"success\":false,\"matched\":false,\"error\":\"encode failed\"}";
        if (json_str != nullptr) {
            cJSON_free(json_str);
        }
        cJSON_Delete(response);
        return result;
    }

    std::string payload = BuildLocatePayload(base64_image, name.c_str(), 0.45);
    if (payload.empty()) {
        return "{\"success\":false,\"matched\":false,\"error\":\"Failed to build request\"}";
    }

    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(3);
    http->SetHeader("Content-Type", "application/json");
    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    http->SetHeader("Client-Id", Board::GetInstance().GetUuid().c_str());
    http->SetContent(std::move(payload));

    ESP_LOGI(TAG, "Sending face locate request to %s for %s", face_track_url_.c_str(), name.c_str());
    if (!http->Open("POST", face_track_url_)) {
        ESP_LOGE(TAG, "Failed to connect to face locate API");
        return "{\"success\":false,\"matched\":false,\"error\":\"Failed to connect to API\"}";
    }

    int status_code = http->GetStatusCode();
    std::string response_string = http->ReadAll();
    http->Close();

    if (status_code != 200) {
        std::string api_error = ExtractApiErrorMessage(response_string, status_code);
        ESP_LOGE(TAG, "Face locate API returned status code %d: %s", status_code, api_error.c_str());

        cJSON* response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddBoolToObject(response, "matched", false);
        cJSON_AddStringToObject(response, "error", api_error.c_str());
        char* json_str = cJSON_PrintUnformatted(response);
        std::string result = json_str ? json_str : "{\"success\":false,\"matched\":false,\"error\":\"API error\"}";
        if (json_str != nullptr) {
            cJSON_free(json_str);
        }
        cJSON_Delete(response);
        return result;
    }

    return response_string;
}

bool Esp32Camera::SetHMirror(bool enabled) {
    sensor_t *s = esp_camera_sensor_get();
    if (s == nullptr) {
        ESP_LOGE(TAG, "Failed to get camera sensor");
        return false;
    }
    
    esp_err_t err = s->set_hmirror(s, enabled);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set horizontal mirror: %d", err);
        return false;
    }
    
    ESP_LOGI(TAG, "Camera horizontal mirror set to: %s", enabled ? "enabled" : "disabled");
    return true;
}

bool Esp32Camera::SetVFlip(bool enabled) {
    sensor_t *s = esp_camera_sensor_get();
    if (s == nullptr) {
        ESP_LOGE(TAG, "Failed to get camera sensor");
        return false;
    }
    
    esp_err_t err = s->set_vflip(s, enabled);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set vertical flip: %d", err);
        return false;
    }
    
    ESP_LOGI(TAG, "Camera vertical flip set to: %s", enabled ? "enabled" : "disabled");
    return true;
}

/**
 * @brief 将摄像头捕获的图像发送到远程服务器进行AI分析和解释
 * 
 * 该函数将当前摄像头缓冲区中的图像编码为JPEG格式，并通过HTTP POST请求
 * 以multipart/form-data的形式发送到指定的解释服务器。服务器将根据提供的
 * 问题对图像进行AI分析并返回结果。
 * 
 * 实现特点：
 * - 使用独立线程编码JPEG，与主线程分离
 * - 采用分块传输编码(chunked transfer encoding)优化内存使用
 * - 通过队列机制实现编码线程和发送线程的数据同步
 * - 支持设备ID、客户端ID和认证令牌的HTTP头部配置
 * 
 * @param question 要向AI提出的关于图像的问题，将作为表单字段发送
 * @return std::string 服务器返回的JSON格式响应字符串
 *         成功时包含AI分析结果，失败时包含错误信息
 *         格式示例：{"success": true, "result": "分析结果"}
 *                  {"success": false, "message": "错误信息"}
 * 
 * @note 调用此函数前必须先调用SetExplainUrl()设置服务器URL
 * @note 函数会等待之前的编码线程完成后再开始新的处理
 * @warning 如果摄像头缓冲区为空或网络连接失败，将返回错误信息
 */
std::string Esp32Camera::Explain(const std::string& question) {
    if (explain_url_.empty()) {
        return "{\"success\": false, \"message\": \"Image explain URL or token is not set\"}";
    }

    // 创建局部的 JPEG 队列, 40 entries is about to store 512 * 40 = 20480 bytes of JPEG data
    QueueHandle_t jpeg_queue = xQueueCreate(40, sizeof(JpegChunk));
    if (jpeg_queue == nullptr) {
        ESP_LOGE(TAG, "Failed to create JPEG queue");
        return "{\"success\": false, \"message\": \"Failed to create JPEG queue\"}";
    }

    // We spawn a thread to encode the image to JPEG
    encoder_thread_ = std::thread([this, jpeg_queue]() {
        frame2jpg_cb(fb_, 80, [](void* arg, size_t index, const void* data, size_t len) -> unsigned int {
            auto jpeg_queue = (QueueHandle_t)arg;
            JpegChunk chunk = {
                .data = (uint8_t*)heap_caps_aligned_alloc(16, len, MALLOC_CAP_SPIRAM),
                .len = len
            };
            memcpy(chunk.data, data, len);
            xQueueSend(jpeg_queue, &chunk, portMAX_DELAY);
            return len;
        }, jpeg_queue);
    });

    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(3);
    // 构造multipart/form-data请求体
    std::string boundary = "----ESP32_CAMERA_BOUNDARY";

    // 配置HTTP客户端，使用分块传输编码
    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    http->SetHeader("Client-Id", Board::GetInstance().GetUuid().c_str());
    if (!explain_token_.empty()) {
        http->SetHeader("Authorization", "Bearer " + explain_token_);
    }
    http->SetHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
    http->SetHeader("Transfer-Encoding", "chunked");
    if (!http->Open("POST", explain_url_)) {
        ESP_LOGE(TAG, "Failed to connect to explain URL");
        // Clear the queue
        encoder_thread_.join();
        JpegChunk chunk;
        while (xQueueReceive(jpeg_queue, &chunk, portMAX_DELAY) == pdPASS) {
            if (chunk.data != nullptr) {
                heap_caps_free(chunk.data);
            } else {
                break;
            }
        }
        vQueueDelete(jpeg_queue);
        return "{\"success\": false, \"message\": \"Failed to connect to explain URL\"}";
    }
    
    {
        // 第一块：question字段
        std::string question_field;
        question_field += "--" + boundary + "\r\n";
        question_field += "Content-Disposition: form-data; name=\"question\"\r\n";
        question_field += "\r\n";
        question_field += question + "\r\n";
        http->Write(question_field.c_str(), question_field.size());
    }
    {
        // 第二块：文件字段头部
        std::string file_header;
        file_header += "--" + boundary + "\r\n";
        file_header += "Content-Disposition: form-data; name=\"file\"; filename=\"camera.jpg\"\r\n";
        file_header += "Content-Type: image/jpeg\r\n";
        file_header += "\r\n";
        http->Write(file_header.c_str(), file_header.size());
    }

    // 第三块：JPEG数据
    size_t total_sent = 0;
    while (true) {
        JpegChunk chunk;
        if (xQueueReceive(jpeg_queue, &chunk, portMAX_DELAY) != pdPASS) {
            ESP_LOGE(TAG, "Failed to receive JPEG chunk");
            break;
        }
        if (chunk.data == nullptr) {
            break; // The last chunk
        }
        http->Write((const char*)chunk.data, chunk.len);
        total_sent += chunk.len;
        heap_caps_free(chunk.data);
    }
    // Wait for the encoder thread to finish
    encoder_thread_.join();
    // 清理队列
    vQueueDelete(jpeg_queue);

    {
        // 第四块：multipart尾部
        std::string multipart_footer;
        multipart_footer += "\r\n--" + boundary + "--\r\n";
        http->Write(multipart_footer.c_str(), multipart_footer.size());
    }
    // 结束块
    http->Write("", 0);

    if (http->GetStatusCode() != 200) {
        ESP_LOGE(TAG, "Failed to upload photo, status code: %d", http->GetStatusCode());
        return "{\"success\": false, \"message\": \"Failed to upload photo\"}";
    }

    std::string result = http->ReadAll();
    http->Close();

    // Get remain task stack size
    size_t remain_stack_size = uxTaskGetStackHighWaterMark(nullptr);
    ESP_LOGI(TAG, "Explain image size=%dx%d, compressed size=%d, remain stack size=%d, question=%s\n%s",
        fb_->width, fb_->height, total_sent, remain_stack_size, question.c_str(), result.c_str());
    return result;
}
