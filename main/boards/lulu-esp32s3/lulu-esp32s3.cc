#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "lamp_controller.h"
#include "led/single_led.h"
#include "esp32_camera.h"
#include <cJSON.h>
#include <cmath>
#include <esp_timer.h>
#include <wifi_station.h>
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <driver/spi_common.h>
#include <driver/uart.h>
#include <driver/gpio.h>
#include "xgo.h"
#include "xgo_action.h"

#if defined(LCD_TYPE_ILI9341_SERIAL)
#include "esp_lcd_ili9341.h"
#endif

#if defined(LCD_TYPE_GC9A01_SERIAL)
#include "esp_lcd_gc9a01.h"
#if DISPLAY_USE_GC9A01_CUSTOM_INIT
static const gc9a01_lcd_init_cmd_t gc9107_lcd_init_cmds[] = {
    //  {cmd, { data }, data_size, delay_ms}
    {0xfe, (uint8_t[]){0x00}, 0, 0},
    {0xef, (uint8_t[]){0x00}, 0, 0},
    {0xb0, (uint8_t[]){0xc0}, 1, 0},
    {0xb1, (uint8_t[]){0x80}, 1, 0},
    {0xb2, (uint8_t[]){0x27}, 1, 0},
    {0xb3, (uint8_t[]){0x13}, 1, 0},
    {0xb6, (uint8_t[]){0x19}, 1, 0},
    {0xb7, (uint8_t[]){0x05}, 1, 0},
    {0xac, (uint8_t[]){0xc8}, 1, 0},
    {0xab, (uint8_t[]){0x0f}, 1, 0},
    {0x3a, (uint8_t[]){0x05}, 1, 0},
    {0xb4, (uint8_t[]){0x04}, 1, 0},
    {0xa8, (uint8_t[]){0x08}, 1, 0},
    {0xb8, (uint8_t[]){0x08}, 1, 0},
    {0xea, (uint8_t[]){0x02}, 1, 0},
    {0xe8, (uint8_t[]){0x2A}, 1, 0},
    {0xe9, (uint8_t[]){0x47}, 1, 0},
    {0xe7, (uint8_t[]){0x5f}, 1, 0},
    {0xc6, (uint8_t[]){0x21}, 1, 0},
    {0xc7, (uint8_t[]){0x15}, 1, 0},
    {0xf0,
    (uint8_t[]){0x1D, 0x38, 0x09, 0x4D, 0x92, 0x2F, 0x35, 0x52, 0x1E, 0x0C,
                0x04, 0x12, 0x14, 0x1f},
    14, 0},
    {0xf1,
    (uint8_t[]){0x16, 0x40, 0x1C, 0x54, 0xA9, 0x2D, 0x2E, 0x56, 0x10, 0x0D,
                0x0C, 0x1A, 0x14, 0x1E},
    14, 0},
    {0xf4, (uint8_t[]){0x00, 0x00, 0xFF}, 3, 0},
    {0xba, (uint8_t[]){0xFF, 0xFF}, 2, 0},
};
#endif
#endif
 
#define TAG "LULUESP32S3"

LV_FONT_DECLARE(font_puhui_14_1);
LV_FONT_DECLARE(font_awesome_14_1);
LV_FONT_DECLARE(font_puhui_16_4);
LV_FONT_DECLARE(font_awesome_16_4);
LV_FONT_DECLARE(font_puhui_20_4);

class LULUESP32S3 : public WifiBoard {
private:
 
    Button boot_button_;
    LcdDisplay* display_;
    Esp32Camera* camera_;

    /*
    * @brief Initialize UART2 communication
    * The baud rate is set to 1 Mbps, with no parity, 8 data bits, 1 stop bit, and hardware flow control disabled.
    * Pin assignment:
    * - TX (Transmit): XGO_UART_TX_PIN
    * - RX (Receive):  XGO_UART_RX_PIN
    */
    void InitializeUart(){
        uart_config_t uart_cfg={
            .baud_rate=1000000,                      
            .data_bits=UART_DATA_8_BITS,  
            .parity=UART_PARITY_DISABLE,  
            .stop_bits=UART_STOP_BITS_1,             
            .flow_ctrl=UART_HW_FLOWCTRL_DISABLE, 
        };
        uart_driver_install(UART_NUM_2,1024,1024,0,NULL,0); 
        uart_param_config(UART_NUM_2,&uart_cfg);
        uart_set_pin(UART_NUM_2, XGO_UART_TX_PIN, XGO_UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        if (XGO_UART_RX_PIN == GPIO_NUM_NC) {
            ESP_LOGW(TAG, "XGO UART RX disabled because no non-conflicting feedback pin is configured");
        }
    }

    /*
    * @brief Initialize GPIO46 as an output for laser control.
    *
    * This function configures GPIO46 to be used as a standard output pin,
    * Configuration details:
    * - Pin: GPIO46
    * - Mode: Output
    * - Pull-up: Disabled
    * - Pull-down: Disabled
    * - Interrupt: Disabled
    */
    void gpio_laser_init() {
        esp_rom_gpio_pad_select_gpio(GPIO_NUM_46);
        gpio_reset_pin(GPIO_NUM_46);

        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << GPIO_NUM_46),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
    }
    
    /*
    * @brief Initialize GPIO0 as an input for the BOOT button.
    * This function configures GPIO0 to serve as an input pin for detecting
    * button presses (commonly used as the BOOT or USER button).
    * Configuration details:
    * - Pin: GPIO0
    * - Mode: Input
    * - Pull-up resistor: Enabled (keeps pin HIGH when button is not pressed)
    * - Pull-down resistor: Disabled
    * - Interrupt: Disabled
    * When the button is pressed, the pin will read LOW (0).
    */
    void gpio_boot_button_init() {
        esp_rom_gpio_pad_select_gpio(GPIO_NUM_0);
        gpio_reset_pin(GPIO_NUM_0);
        
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << GPIO_NUM_0),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE, 
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&io_conf));        
        ESP_LOGI(TAG, "GPIO_NUM_0 configured as input with pull-up resistor");
    }
    
    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        //LCD Screen Control IO Initialization
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = DISPLAY_SPI_CLOCK_HZ;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        //Initialize the LCD driver chip
        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
#if defined(LCD_TYPE_ILI9341_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));
#elif defined(LCD_TYPE_GC9A01_SERIAL)
#if DISPLAY_USE_GC9A01_CUSTOM_INIT
        gc9a01_vendor_config_t gc9107_vendor_config = {
            .init_cmds = gc9107_lcd_init_cmds,
            .init_cmds_size = sizeof(gc9107_lcd_init_cmds) / sizeof(gc9a01_lcd_init_cmd_t),
        };
        panel_config.vendor_config = &gc9107_vendor_config;
#endif
        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(panel_io, &panel_config, &panel));
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
#endif
        
        esp_lcd_panel_reset(panel);
 

        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        display_ = new SpiLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
                                    {
                                        .text_font = &font_puhui_20_4,
                                        .icon_font = &font_awesome_16_4,
#if CONFIG_USE_WECHAT_MESSAGE_STYLE
                                        .emoji_font = font_emoji_32_init(),
#else
                                        .emoji_font = DISPLAY_HEIGHT >= 240 ? font_emoji_64_init() : font_emoji_32_init(),
#endif
                                    });
    }

    void InitializeCamera() {
        camera_config_t config = {};
        config.pin_d0 = CAMERA_PIN_D0;
        config.pin_d1 = CAMERA_PIN_D1;
        config.pin_d2 = CAMERA_PIN_D2;
        config.pin_d3 = CAMERA_PIN_D3;
        config.pin_d4 = CAMERA_PIN_D4;
        config.pin_d5 = CAMERA_PIN_D5;
        config.pin_d6 = CAMERA_PIN_D6;
        config.pin_d7 = CAMERA_PIN_D7;
        config.pin_xclk = CAMERA_PIN_XCLK;
        config.pin_pclk = CAMERA_PIN_PCLK;
        config.pin_vsync = CAMERA_PIN_VSYNC;
        config.pin_href = CAMERA_PIN_HREF;
        config.pin_sccb_sda = CAMERA_PIN_SIOD;  
        config.pin_sccb_scl = CAMERA_PIN_SIOC;
        config.sccb_i2c_port = 0;
        config.pin_pwdn = CAMERA_PIN_PWDN;
        config.pin_reset = CAMERA_PIN_RESET;
        config.xclk_freq_hz = XCLK_FREQ_HZ;
        config.pixel_format = PIXFORMAT_RGB565;
        config.frame_size = FRAMESIZE_QVGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_PSRAM;
        config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
        camera_ = new Esp32Camera(config);
        camera_->SetHMirror(false);
        camera_->SetFaceEnrollUrl(FACE_ENROLL_URL);
        camera_->SetFaceTrackUrl(FACE_TRACK_URL);
    }

    
    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });
    }

    /*
    * @brief Set the robot dog's movement speed and duration
    * @param dog_vx   Forward speed (positive for forward, negative for backward)
    * @param dog_vyaw Rotational speed (positive for left turn, negative for right turn)
    * @param time     Duration in milliseconds
    */
    void set_dog_speed(int dog_vx, int dog_vyaw, int time)
    {        
        motor_speed = 0;
        vx = 3.0*dog_vx;
        vyaw = 3.0*dog_vyaw;
        if(time>0){
            vTaskDelay(pdMS_TO_TICKS(time));
        }
        vx = 0.0;
        vyaw = 0.0;
    }

    struct FollowCommand {
        int dog_vx;
        int dog_vyaw;
        const char* turn;
        const char* distance;
    };

    struct FollowSummaryStats {
        int completed_steps = 0;
        int tracked_steps = 0;
        int moved_steps = 0;
        double final_offset_x = 0.0;
        double final_face_ratio = 0.0;
        bool has_final_pose = false;
    };

    FollowCommand DecideFollowCommand(double offset_x, double face_ratio) {
        int dog_vyaw = 0;
        int dog_vx = 0;
        double abs_offset = std::fabs(offset_x);

        if (abs_offset > 0.30) {
            dog_vyaw = offset_x > 0 ? -72 : 72;
        } else if (abs_offset > 0.20) {
            dog_vyaw = offset_x > 0 ? -56 : 56;
        } else if (abs_offset > 0.12) {
            dog_vyaw = offset_x > 0 ? -46 : 46;
        } else if (abs_offset > 0.06) {
            dog_vyaw = offset_x > 0 ? -30 : 30;
        }

        if (face_ratio < 0.05) {
            if (abs_offset > 0.30) {
                dog_vx = 8;
            } else if (abs_offset > 0.20) {
                dog_vx = 12;
            } else if (abs_offset > 0.12) {
                dog_vx = 18;
            } else {
                dog_vx = 28;
            }
        } else if (face_ratio < 0.10) {
            if (abs_offset > 0.30) {
                dog_vx = 6;
            } else if (abs_offset > 0.20) {
                dog_vx = 10;
            } else if (abs_offset > 0.12) {
                dog_vx = 14;
            } else {
                dog_vx = 20;
            }
        } else if (face_ratio > 0.28) {
            dog_vx = -18;
        } else if (face_ratio > 0.20) {
            dog_vx = -10;
        }

        const char* turn = "center";
        if (dog_vyaw > 0) {
            turn = "left";
        } else if (dog_vyaw < 0) {
            turn = "right";
        }

        const char* distance = "hold";
        if (dog_vx > 0) {
            distance = "forward";
        } else if (dog_vx < 0) {
            distance = "backward";
        }

        return {dog_vx, dog_vyaw, turn, distance};
    }

    std::string BuildFollowSummary(
        const std::string& target_name,
        int requested_steps,
        int step_time_ms,
        cJSON* steps,
        const FollowSummaryStats& stats,
        bool success,
        const char* error = nullptr
    ) {
        cJSON* root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "success", success);
        cJSON_AddStringToObject(root, "status", success ? "follow_burst_completed" : "follow_burst_failed");
        cJSON_AddStringToObject(root, "target_name", target_name.c_str());
        cJSON_AddNumberToObject(root, "requested_steps", requested_steps);
        cJSON_AddNumberToObject(root, "completed_steps", stats.completed_steps);
        cJSON_AddNumberToObject(root, "tracked_steps", stats.tracked_steps);
        cJSON_AddNumberToObject(root, "moved_steps", stats.moved_steps);
        cJSON_AddBoolToObject(root, "target_visible", stats.tracked_steps > 0);
        cJSON_AddBoolToObject(root, "moved", stats.moved_steps > 0);
        cJSON_AddNumberToObject(root, "step_time_ms", step_time_ms);
        if (stats.has_final_pose) {
            cJSON_AddNumberToObject(root, "final_offset_x", stats.final_offset_x);
            cJSON_AddNumberToObject(root, "final_face_ratio", stats.final_face_ratio);
        }

        char message[256];
        if (success) {
            snprintf(
                message,
                sizeof(message),
                "Follow burst completed. Target tracked for %d/%d steps and dog moved for %d steps.",
                stats.tracked_steps,
                requested_steps,
                stats.moved_steps
            );
        } else {
            snprintf(
                message,
                sizeof(message),
                "Follow burst failed after %d/%d steps: %s",
                stats.completed_steps,
                requested_steps,
                error != nullptr ? error : "unknown error"
            );
        }
        cJSON_AddStringToObject(root, "message", message);
        cJSON_AddItemToObject(root, "steps", steps);
        if (error != nullptr) {
            cJSON_AddStringToObject(root, "error", error);
        }

        char* json_str = cJSON_PrintUnformatted(root);
        std::string result = json_str ? json_str : "{\"success\":false,\"error\":\"Failed to build follow summary\"}";
        if (json_str != nullptr) {
            cJSON_free(json_str);
        }
        cJSON_Delete(root);
        return result;
    }

    std::string FollowPerson(const std::string& target_name, int steps, int step_time_ms) {
        if (camera_ == nullptr) {
            return "{\"success\":false,\"error\":\"Camera not initialized\"}";
        }

        const int planned_steps = std::max(1, std::min(steps, 4));
        const int planned_step_time_ms = std::max(180, std::min(step_time_ms, 280));
        const int max_follow_elapsed_ms = 8500;
        int64_t follow_started_us = esp_timer_get_time();
        cJSON* steps_json = cJSON_CreateArray();
        FollowSummaryStats stats;

        for (int i = 0; i < planned_steps; ++i) {
            int64_t step_started_us = esp_timer_get_time();
            if (!camera_->Capture()) {
                cJSON* step_json = cJSON_CreateObject();
                cJSON_AddNumberToObject(step_json, "step", i + 1);
                cJSON_AddStringToObject(step_json, "error", "Failed to capture photo");
                cJSON_AddItemToArray(steps_json, step_json);
                stats.completed_steps = i;
                return BuildFollowSummary(target_name, planned_steps, planned_step_time_ms, steps_json, stats, false, "Failed to capture photo");
            }

            std::string locate_result = camera_->LocatePerson(target_name);
            cJSON* locate_json = cJSON_Parse(locate_result.c_str());
            if (locate_json == nullptr) {
                cJSON* step_json = cJSON_CreateObject();
                cJSON_AddNumberToObject(step_json, "step", i + 1);
                cJSON_AddStringToObject(step_json, "error", "Invalid locate response");
                cJSON_AddItemToArray(steps_json, step_json);
                stats.completed_steps = i;
                return BuildFollowSummary(target_name, planned_steps, planned_step_time_ms, steps_json, stats, false, "Invalid locate response");
            }

            cJSON* success = cJSON_GetObjectItem(locate_json, "success");
            cJSON* matched = cJSON_GetObjectItem(locate_json, "matched");
            cJSON* confidence = cJSON_GetObjectItem(locate_json, "confidence");
            cJSON* detected_name = cJSON_GetObjectItem(locate_json, "detected_name");
            cJSON* offset = cJSON_GetObjectItem(locate_json, "offset");
            cJSON* face_ratio = cJSON_GetObjectItem(locate_json, "face_ratio");
            cJSON* error = cJSON_GetObjectItem(locate_json, "error");
            cJSON* processing_time_ms = cJSON_GetObjectItem(locate_json, "processing_time_ms");

            cJSON* step_json = cJSON_CreateObject();
            cJSON_AddNumberToObject(step_json, "step", i + 1);
            cJSON_AddBoolToObject(step_json, "success", cJSON_IsTrue(success));
            cJSON_AddBoolToObject(step_json, "matched", cJSON_IsTrue(matched));
            stats.completed_steps = i + 1;
            if (cJSON_IsString(detected_name) && detected_name->valuestring != nullptr) {
                cJSON_AddStringToObject(step_json, "detected_name", detected_name->valuestring);
            }
            if (cJSON_IsNumber(confidence)) {
                cJSON_AddNumberToObject(step_json, "confidence", confidence->valuedouble);
            }
            if (cJSON_IsNumber(processing_time_ms)) {
                cJSON_AddNumberToObject(step_json, "processing_time_ms", processing_time_ms->valueint);
            }
            if (cJSON_IsString(error) && error->valuestring != nullptr) {
                cJSON_AddStringToObject(step_json, "error", error->valuestring);
            }

            if (!cJSON_IsTrue(success) || !cJSON_IsTrue(matched) || !cJSON_IsObject(offset) || !cJSON_IsNumber(face_ratio)) {
                set_dog_speed(0, 0, 0);
                cJSON_AddItemToArray(steps_json, step_json);
                cJSON_Delete(locate_json);
                return BuildFollowSummary(target_name, planned_steps, planned_step_time_ms, steps_json, stats, false, "Target not matched");
            }

            cJSON* offset_x = cJSON_GetObjectItem(offset, "x");
            if (!cJSON_IsNumber(offset_x)) {
                set_dog_speed(0, 0, 0);
                cJSON_AddStringToObject(step_json, "error", "Missing horizontal offset");
                cJSON_AddItemToArray(steps_json, step_json);
                cJSON_Delete(locate_json);
                return BuildFollowSummary(target_name, planned_steps, planned_step_time_ms, steps_json, stats, false, "Missing horizontal offset");
            }

            FollowCommand command = DecideFollowCommand(offset_x->valuedouble, face_ratio->valuedouble);
            int locate_processing_ms = cJSON_IsNumber(processing_time_ms) ? processing_time_ms->valueint : -1;
            stats.tracked_steps += 1;
            stats.final_offset_x = offset_x->valuedouble;
            stats.final_face_ratio = face_ratio->valuedouble;
            stats.has_final_pose = true;
            ESP_LOGI(
                TAG,
                "follow step=%d locate_ms=%d offset_x=%.3f face_ratio=%.3f vx=%d vyaw=%d",
                i + 1,
                locate_processing_ms,
                offset_x->valuedouble,
                face_ratio->valuedouble,
                command.dog_vx,
                command.dog_vyaw
            );
            cJSON* movement = cJSON_CreateObject();
            cJSON_AddNumberToObject(movement, "dog_vx", command.dog_vx);
            cJSON_AddNumberToObject(movement, "dog_vyaw", command.dog_vyaw);
            cJSON_AddNumberToObject(movement, "time_ms", planned_step_time_ms);
            cJSON_AddStringToObject(movement, "turn", command.turn);
            cJSON_AddStringToObject(movement, "distance", command.distance);
            cJSON_AddItemToObject(step_json, "movement", movement);

            cJSON_AddItemToArray(steps_json, step_json);
            cJSON_Delete(locate_json);

            if (command.dog_vx != 0 || command.dog_vyaw != 0) {
                stats.moved_steps += 1;
            }
            set_dog_speed(command.dog_vx, command.dog_vyaw, planned_step_time_ms);
            int step_elapsed_ms = static_cast<int>((esp_timer_get_time() - step_started_us) / 1000);
            ESP_LOGI(
                TAG,
                "follow step=%d done total_step_ms=%d",
                i + 1,
                step_elapsed_ms
            );

            int follow_elapsed_ms = static_cast<int>((esp_timer_get_time() - follow_started_us) / 1000);
            if (follow_elapsed_ms >= max_follow_elapsed_ms) {
                cJSON_AddBoolToObject(step_json, "truncated_for_timeout_budget", true);
                cJSON_AddNumberToObject(step_json, "follow_elapsed_ms", follow_elapsed_ms);
                ESP_LOGW(TAG, "follow burst stopped early at step=%d elapsed_ms=%d to avoid upstream timeout", i + 1, follow_elapsed_ms);
                return BuildFollowSummary(target_name, planned_steps, planned_step_time_ms, steps_json, stats, true);
            }
            vTaskDelay(pdMS_TO_TICKS(40));
        }

        cJSON* final_step = cJSON_GetArraySize(steps_json) > 0 ? cJSON_GetArrayItem(steps_json, cJSON_GetArraySize(steps_json) - 1) : nullptr;
        if (final_step != nullptr) {
            cJSON_AddNumberToObject(final_step, "follow_elapsed_ms", (esp_timer_get_time() - follow_started_us) / 1000);
        }

        return BuildFollowSummary(target_name, planned_steps, planned_step_time_ms, steps_json, stats, true);
    }

    /*
    * @brief Control the robot to enter or exit motor calibration mode
    * @param mode Calibration mode switch:
    *              - 1: Enter calibration mode (disable all motors)
    *              - 0: Exit calibration mode (save zero position and re-enable motors)
    */
    void Calibrate(int mode){
        if(mode==1 && calibrate_mode==0){
            EnableAllMotor(0);
            calibrate_mode = 1;
        }
        if(mode==0 && calibrate_mode==1){
            WriteZeroPos();
            EnableAllMotor(1);
            calibrate_mode = 0;            
        }
    }
    
    /* Laser sword switch control pin
    * @brief Enumeration for GPIO control modes
    * Defines three GPIO output control states:
    *              - Off (0): Turn off GPIO output  
    *              - On (1): Turn on GPIO output  
    *              - Toggle (2): Toggle the GPIO state  
    */

    enum class GpioMode {
    Off = 0,
    On = 1,
    Toggle = 2
    };
    
    /*
    * @brief Control the output level of GPIO46 
    * Sets the output level of GPIO46 according to the specified @ref GpioMode:
    *              - `GpioMode::Off`→ Set to low level  
    *              - `GpioMode::On`→ Set to high level  
    *              - `GpioMode::Toggle` → Perform a toggle operation (switch off then on)
    */
    void control_gpio(GpioMode mode) {
        switch (mode) {
            case GpioMode::Off:
                gpio_set_level(GPIO_NUM_46, 0);
                break;
            case GpioMode::On:
                gpio_set_level(GPIO_NUM_46, 1);
                break;
            case GpioMode::Toggle:
                gpio_set_level(GPIO_NUM_46, 0);
                printf("Switch lighting modes\n");
                gpio_set_level(GPIO_NUM_46, 1);
                break;
        }
    }
    
    //IoT initialization, adding support for AI-visible devices
    void InitializeIot() {
        auto& mcp_server = McpServer::GetInstance();

        mcp_server.AddTool("self.camera.face_rec",
        "识别人脸专用：拍一张照片，并在本地已有人脸库里判断这是谁。这个工具只做识别，不会新增或保存任何人脸。用户问'这是谁'、'认出我是谁'时使用。",
        PropertyList(std::vector<Property>{}), [this](const PropertyList& properties) -> ReturnValue {
            (void)properties;
            if (camera_ == nullptr) {
                return std::string("<rec>Camera not initialized</rec>");
            }
            if (!camera_->Capture()) {
                return std::string("<rec>Failed to capture photo</rec>");
            }
            return camera_->RecognizeFace(FACE_RECOGNITION_URL);
        });

        mcp_server.AddTool("self.camera.face_enroll",
        "录入人脸专用：拍一张照片，把当前这个人保存到本地人脸库。用户说'记住我'、'录入我'、'注册人脸'、'把这个人记成某某'时必须使用这个工具，而不是识别工具。必填参数：name。",
        PropertyList({
            Property("name", kPropertyTypeString)
        }), [this](const PropertyList& properties) -> ReturnValue {
            if (camera_ == nullptr) {
                return std::string("<enroll>Camera not initialized</enroll>");
            }
            if (!camera_->Capture()) {
                return std::string("<enroll>Failed to capture photo</enroll>");
            }
            return camera_->EnrollFace(FACE_ENROLL_URL, properties["name"].value<std::string>());
        });

        mcp_server.AddTool("self.camera.remember_person",
        "记住某人：这是录入人脸的同义工具。用户说'记住我叫某某'、'记住这个人叫某某'时使用。会拍照并把人脸保存到本地数据库。必填参数：name。",
        PropertyList({
            Property("name", kPropertyTypeString)
        }), [this](const PropertyList& properties) -> ReturnValue {
            if (camera_ == nullptr) {
                return std::string("<enroll>Camera not initialized</enroll>");
            }
            if (!camera_->Capture()) {
                return std::string("<enroll>Failed to capture photo</enroll>");
            }
            return camera_->EnrollFace(FACE_ENROLL_URL, properties["name"].value<std::string>());
        });

        mcp_server.AddTool("self.camera.locate_person",
        "Capture a photo and locate the named person in the frame. Returns whether the target matched, the detected identity, face position, and coarse movement hints. Use this before follow or when you need to know where someone is.",
        PropertyList({
            Property("name", kPropertyTypeString)
        }), [this](const PropertyList& properties) -> ReturnValue {
            if (camera_ == nullptr) {
                return std::string("{\"success\":false,\"error\":\"Camera not initialized\"}");
            }
            if (!camera_->Capture()) {
                return std::string("{\"success\":false,\"matched\":false,\"error\":\"Failed to capture photo\"}");
            }
            return camera_->LocatePerson(properties["name"].value<std::string>());
        });

        mcp_server.AddTool("self.dog.move", 
        "机器狗移动(vx,vyaw,time),前后移动速度vx(前正后负,0停下)和转向速度vyaw(左转正值,右转负值,0停下),time为移动时间(毫秒),time=0时持续移动,否则移动time毫秒后停止", 
        PropertyList({
            Property("dog_vx", kPropertyTypeInteger, -100, 100),
            Property("dog_vyaw", kPropertyTypeInteger, -100, 100),
            Property("time", kPropertyTypeInteger, 0, 10000),
        }), [this](const PropertyList& properties) -> ReturnValue {
            int dog_vx = properties["dog_vx"].value<int>();
            int dog_vyaw = properties["dog_vyaw"].value<int>();
            int time = properties["time"].value<int>();
            set_dog_speed(dog_vx, dog_vyaw, time);
            return true;
        });

        mcp_server.AddTool("self.dog.follow_person",
        "Short closed-loop follow burst. Capture photos, keep the named person in view, and move the dog a few small steps toward that person using face offset and face size. success=true means this follow burst completed normally. The dog does not need to fully reach the person for the tool to count as success.",
        PropertyList({
            Property("name", kPropertyTypeString),
            Property("steps", kPropertyTypeInteger, 4, 1, 4),
            Property("step_time_ms", kPropertyTypeInteger, 260, 180, 600),
        }), [this](const PropertyList& properties) -> ReturnValue {
            return FollowPerson(
                properties["name"].value<std::string>(),
                properties["steps"].value<int>(),
                properties["step_time_ms"].value<int>()
            );
        });

        mcp_server.AddTool("self.dog.calibrate", 
        "标定机器狗,1为进入标定,0为退出/完成标定", 
        PropertyList({
            Property("mode", kPropertyTypeInteger, 0, 1),
        }), [this](const PropertyList& properties) -> ReturnValue {
            int mode = properties["mode"].value<int>();
            Calibrate(mode);
            return true;
        });

        mcp_server.AddTool("self.dog.Wave", 
            "执行打招呼动作", 
            PropertyList(std::vector<Property>{}),
            [this](const PropertyList& properties) -> ReturnValue {
                Action_ID = Wave_ID;  
                return true;
            });
        
        mcp_server.AddTool("self.dog.Naughty", 
            "执行撒娇动作", 
            PropertyList(std::vector<Property>{}),
            [this](const PropertyList& properties) -> ReturnValue {
                Action_ID = Naughty_ID;  
                return true;
            });

        mcp_server.AddTool("self.dog.Swing", 
            "执行前后运动动作", 
            PropertyList(std::vector<Property>{}),
            [this](const PropertyList& properties) -> ReturnValue {
                Action_ID = Swing_ID;  
                return true;
            });

        mcp_server.AddTool("self.dog.Lookup", 
            "执行祈求/抬头动作", 
            PropertyList(std::vector<Property>{}),
            [this](const PropertyList& properties) -> ReturnValue {
                Action_ID = Lookup_ID;  
                return true;
            });

        mcp_server.AddTool("self.dog.Rolling", 
            "执行左右摇摆动作", 
            PropertyList(std::vector<Property>{}),
            [this](const PropertyList& properties) -> ReturnValue {
                Action_ID = Rolling_ID;  
                return true;
            });  

        mcp_server.AddTool("self.dog.Angry", 
            "执行懊悔/生气动作", 
            PropertyList(std::vector<Property>{}),
            [this](const PropertyList& properties) -> ReturnValue {
                Action_ID = Angry_ID;  
                return true;
            });

        mcp_server.AddTool("self.dog.Swimming", 
            "执行游泳动作", 
            PropertyList(std::vector<Property>{}),
            [this](const PropertyList& properties) -> ReturnValue {
                Action_ID = Swimming_ID;  
                return true;
            });

        mcp_server.AddTool("self.dog.Pee", 
            "执行撒尿动作", 
            PropertyList(std::vector<Property>{}),
            [this](const PropertyList& properties) -> ReturnValue {
                Action_ID = Pee_ID;  
                return true;
            });

        mcp_server.AddTool("self.dog.Stretch", 
            "执行伸懒腰动作", 
            PropertyList(std::vector<Property>{}),
            [this](const PropertyList& properties) -> ReturnValue {
                Action_ID = Stretch_ID;  
                return true;
            });

        mcp_server.AddTool("self.dog.Bouncing", 
            "执行上下蹲起动作", 
            PropertyList(std::vector<Property>{}),
            [this](const PropertyList& properties) -> ReturnValue {
                Action_ID = Bouncing_ID;  
                return true;
            });

        mcp_server.AddTool("self.dog.Shaking", 
            "执行摇头晃脑动作", 
            PropertyList(std::vector<Property>{}),
            [this](const PropertyList& properties) -> ReturnValue {
                Action_ID = Shaking_ID;  
                return true;
            });
        mcp_server.AddTool("self.dog.Sit", 
            "执行坐下动作", 
            PropertyList(std::vector<Property>{}),
            [this](const PropertyList& properties) -> ReturnValue {
                Action_ID = Sit_ID;  
                return true;
            });

        mcp_server.AddTool("self.dog.Scratch", 
            "执行挠痒动作", 
            PropertyList(std::vector<Property>{}),
            [this](const PropertyList& properties) -> ReturnValue {
                Action_ID = Scratch_ID;  
                return true;
            });

        mcp_server.AddTool("self.dog.Hug", 
            "执行抱抱动作", 
            PropertyList(std::vector<Property>{}),
            [this](const PropertyList& properties) -> ReturnValue {
                Action_ID = Hug_ID;  
                return true;
            });

        mcp_server.AddTool("self.dog.Reset", 
            "执行复位动作", 
            PropertyList(std::vector<Property>{}),
            [this](const PropertyList& properties) -> ReturnValue {
                Clear_State(2); 
                return true;
            });

        mcp_server.AddTool("self.dog.action_loop", 
            "设定表演模式/动作循环,1为开始,0为停止", 
            PropertyList({
                Property("flag", kPropertyTypeInteger, 0, 1),
            }), [this](const PropertyList& properties) -> ReturnValue {
                int flag = properties["flag"].value<int>();
                set_action_loop_flag(flag);
                return true;
            });
 
        mcp_server.AddTool("self.laser.control",
            "激光剑控制: 0=关闭, 1=打开, 2=切换激光剑模式",
            PropertyList({
                Property("mode", kPropertyTypeInteger, 0, 2),
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                int modeValue = properties["mode"].value<int>();

                if (modeValue < 0 || modeValue > 2) {
                    printf("错误: 无效的 mode 值: %d\n", modeValue);
                    return false;
                }

                control_gpio(static_cast<GpioMode>(modeValue));
                return true;
            }
        );  
    }

public:
    LULUESP32S3() :
        boot_button_(BOOT_BUTTON_GPIO) {
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeButtons();
        InitializeIot();
        InitializeCamera();        
        InitializeUart();
        gpio_laser_init();
        InitZeroPos();
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            GetBacklight()->RestoreBrightness();
        }
        
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#else
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            return &backlight;
        }
        return nullptr;
    }

    virtual Camera* GetCamera() override {
        return camera_;
    }
};

DECLARE_BOARD(LULUESP32S3);
