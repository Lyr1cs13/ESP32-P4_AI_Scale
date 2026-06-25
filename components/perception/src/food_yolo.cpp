#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "dl_define.hpp"
#include "dl_image_define.hpp"
#include "dl_image_preprocessor.hpp"
#include "dl_math.hpp"
#include "dl_model_base.hpp"
#include "dl_tensor_base.hpp"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "food_result.h"
#include "perception_internal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "linux/videodev2.h"

extern "C" esp_err_t latest_frame_get(uint8_t **buf,
                                      uint32_t *size,
                                      uint32_t *width,
                                      uint32_t *height,
                                      uint32_t *pixel_format);

static const char *TAG = "food_yolo11n";
static TaskHandle_t s_yolo_task_handle = nullptr;
static QueueHandle_t s_inference_queue = nullptr;
static constexpr const char *MODEL_PARTITION_LABEL = "yolo_model";
static constexpr float CONF_THRESH = 0.60f;
static constexpr float NMS_THRESH = 0.70f;
static constexpr int TOP_K = 20;
static constexpr int NUM_CLASSES = 31;
static constexpr int GRID0 = 20;
static constexpr int GRID1 = 10;
static constexpr int GRID2 = 5;
static constexpr int NUM_BOXES = GRID0 * GRID0 + GRID1 * GRID1 + GRID2 * GRID2;
static constexpr int INFERENCE_QUEUE_LEN = 1;
static constexpr int FOOD_ITEM_BUFFER_LEN = 64;
static const char *SCORE_OUTPUT_NAMES[3] = {
    "/model.23/cv3.0/cv3.0.2/Conv_output_0",
    "/model.23/cv3.1/cv3.1.2/Conv_output_0",
    "/model.23/cv3.2/cv3.2.2/Conv_output_0",
};

static void log_model_partition_header()
{
    const esp_partition_t *partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, MODEL_PARTITION_LABEL);
    if (partition == nullptr) {
        ESP_LOGE(TAG, "model partition '%s' not found", MODEL_PARTITION_LABEL);
        return;
    }

    uint8_t header[16] = {};
    esp_err_t ret = esp_partition_read(partition, 0, header, sizeof(header));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to read model partition header: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG,
             "model partition '%s': offset=0x%" PRIx32 ", size=%" PRIu32 " KB, header=%02x %02x %02x %02x '%c%c%c%c'",
             MODEL_PARTITION_LABEL,
             partition->address,
             partition->size / 1024,
             header[0],
             header[1],
             header[2],
             header[3],
             header[0] >= 32 && header[0] <= 126 ? header[0] : '.',
             header[1] >= 32 && header[1] <= 126 ? header[1] : '.',
             header[2] >= 32 && header[2] <= 126 ? header[2] : '.',
             header[3] >= 32 && header[3] <= 126 ? header[3] : '.');
}

static const std::array<const char *, NUM_CLASSES> CLASS_NAMES = {
    "apple",
    "banana",
    "beef",
    "bell_pepper",
    "cabbage",
    "carrot",
    "cauliflower",
    "chicken",
    "cucumber",
    "egg",
    "eggplant",
    "fish",
    "garlic",
    "ginger",
    "grape",
    "kiwi",
    "kumquat",
    "lemon",
    "onion",
    "orange",
    "peach",
    "pepper",
    "pineapple",
    "pork",
    "potato",
    "shrimp",
    "small_pepper",
    "strawberry",
    "tofu",
    "tomato",
    "watermelon",
};

typedef struct {
    int category;
    float score;
    int x1;
    int y1;
    int x2;
    int y2;
} detection_t;

typedef struct {
    float total_weight_g;
    float item_weight_g;
    int64_t timestamp_us;
} inference_request_t;

static food_item_record_t s_item_buffer[FOOD_ITEM_BUFFER_LEN] = {};
static int s_item_write_index = 0;
static int s_item_count = 0;
static uint32_t s_item_sequence = 0;
static food_class_total_t s_class_totals[NUM_CLASSES] = {};
static SemaphoreHandle_t s_result_lock = nullptr;

static void copy_class_name(char *dst, size_t dst_size, int category)
{
    if (dst_size == 0) {
        return;
    }

    const char *name = (category >= 0 && category < NUM_CLASSES) ? CLASS_NAMES[category] : "unknown";
    std::snprintf(dst, dst_size, "%s", name);
}

static void init_result_store()
{
    if (!s_result_lock) {
        s_result_lock = xSemaphoreCreateMutex();
    }

    for (int i = 0; i < NUM_CLASSES; ++i) {
        s_class_totals[i].category = i;
        copy_class_name(s_class_totals[i].class_name, sizeof(s_class_totals[i].class_name), i);
        s_class_totals[i].count = 0;
        s_class_totals[i].total_weight_g = 0.0f;
    }
}

static void store_food_item(const inference_request_t &request, const detection_t &best)
{
    if (!s_result_lock) {
        return;
    }
    if (xSemaphoreTake(s_result_lock, portMAX_DELAY) != pdPASS) {
        return;
    }

    food_item_record_t &item = s_item_buffer[s_item_write_index];
    item.sequence = ++s_item_sequence;
    item.category = best.category;
    copy_class_name(item.class_name, sizeof(item.class_name), best.category);
    item.item_weight_g = request.item_weight_g;
    item.total_weight_g = request.total_weight_g;
    item.score = best.score;
    item.timestamp_us = request.timestamp_us;

    s_item_write_index = (s_item_write_index + 1) % FOOD_ITEM_BUFFER_LEN;
    if (s_item_count < FOOD_ITEM_BUFFER_LEN) {
        ++s_item_count;
    }

    if (best.category >= 0 && best.category < NUM_CLASSES) {
        s_class_totals[best.category].count++;
        s_class_totals[best.category].total_weight_g += request.item_weight_g;
    }

    xSemaphoreGive(s_result_lock);
}

static float iou(const detection_t &a, const detection_t &b)
{
    const int xx1 = std::max(a.x1, b.x1);
    const int yy1 = std::max(a.y1, b.y1);
    const int xx2 = std::min(a.x2, b.x2);
    const int yy2 = std::min(a.y2, b.y2);
    const int w = std::max(0, xx2 - xx1 + 1);
    const int h = std::max(0, yy2 - yy1 + 1);
    const int inter = w * h;
    const int area_a = std::max(0, a.x2 - a.x1 + 1) * std::max(0, a.y2 - a.y1 + 1);
    const int area_b = std::max(0, b.x2 - b.x1 + 1) * std::max(0, b.y2 - b.y1 + 1);
    const int denom = area_a + area_b - inter;
    return denom > 0 ? static_cast<float>(inter) / static_cast<float>(denom) : 0.0f;
}

static float output_value(dl::TensorBase *output, int channel, int index)
{
    const int offset = channel * NUM_BOXES + index;
    const float scale = DL_SCALE(output->exponent);

    if (output->dtype == dl::DATA_TYPE_INT8) {
        return dl::dequantize(static_cast<int8_t *>(output->data)[offset], scale);
    }
    if (output->dtype == dl::DATA_TYPE_INT16) {
        return dl::dequantize(static_cast<int16_t *>(output->data)[offset], scale);
    }
    return static_cast<float *>(output->data)[offset];
}

static float score_from_output(float value)
{
    return dl::math::sigmoid(value);
}

static float score_logit_value(dl::TensorBase *score0, dl::TensorBase *score1, dl::TensorBase *score2, int cls, int index)
{
    dl::TensorBase *score = nullptr;
    int local_index = index;
    int grid = GRID0;

    if (index < GRID0 * GRID0) {
        score = score0;
    } else if (index < GRID0 * GRID0 + GRID1 * GRID1) {
        score = score1;
        local_index = index - GRID0 * GRID0;
        grid = GRID1;
    } else {
        score = score2;
        local_index = index - GRID0 * GRID0 - GRID1 * GRID1;
        grid = GRID2;
    }

    const int y = local_index / grid;
    const int x = local_index % grid;
    const int offset = ((y * grid + x) * NUM_CLASSES) + cls;

    if (score->dtype == dl::DATA_TYPE_FLOAT) {
        return static_cast<float *>(score->data)[offset];
    }

    const float scale = DL_SCALE(score->exponent);
    if (score->dtype == dl::DATA_TYPE_INT8) {
        return dl::dequantize(static_cast<int8_t *>(score->data)[offset], scale);
    }
    return dl::dequantize(static_cast<int16_t *>(score->data)[offset], scale);
}

static std::vector<detection_t> parse_yolo_output(dl::TensorBase *output,
                                                  dl::TensorBase *score0,
                                                  dl::TensorBase *score1,
                                                  dl::TensorBase *score2,
                                                  dl::image::ImagePreprocessor &preprocessor,
                                                  int image_w,
                                                  int image_h)
{
    std::vector<detection_t> candidates;
    candidates.reserve(TOP_K * 4);

    const float inv_scale_x = preprocessor.get_resize_scale_x(true);
    const float inv_scale_y = preprocessor.get_resize_scale_y(true);
    const int border_left = preprocessor.get_border_left();
    const int border_top = preprocessor.get_border_top();

    for (int i = 0; i < NUM_BOXES; ++i) {
        int best_class = -1;
        float best_score = 0.0f;

        for (int c = 0; c < NUM_CLASSES; ++c) {
            const float score = score_from_output(score_logit_value(score0, score1, score2, c, i));
            if (score > best_score) {
                best_score = score;
                best_class = c;
            }
        }

        if (best_score < CONF_THRESH) {
            continue;
        }

        const float cx = output_value(output, 0, i);
        const float cy = output_value(output, 1, i);
        const float w = output_value(output, 2, i);
        const float h = output_value(output, 3, i);

        detection_t det = {
            best_class,
            best_score,
            static_cast<int>(((cx - w * 0.5f) - border_left) * inv_scale_x),
            static_cast<int>(((cy - h * 0.5f) - border_top) * inv_scale_y),
            static_cast<int>(((cx + w * 0.5f) - border_left) * inv_scale_x),
            static_cast<int>(((cy + h * 0.5f) - border_top) * inv_scale_y),
        };

        det.x1 = DL_CLIP(det.x1, 0, image_w - 1);
        det.y1 = DL_CLIP(det.y1, 0, image_h - 1);
        det.x2 = DL_CLIP(det.x2, 0, image_w - 1);
        det.y2 = DL_CLIP(det.y2, 0, image_h - 1);

        if (det.x2 > det.x1 && det.y2 > det.y1) {
            candidates.push_back(det);
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const detection_t &a, const detection_t &b) {
        return a.score > b.score;
    });

    std::vector<detection_t> results;
    results.reserve(TOP_K);
    for (const auto &candidate : candidates) {
        bool keep = true;
        for (const auto &kept : results) {
            if (candidate.category == kept.category && iou(candidate, kept) > NMS_THRESH) {
                keep = false;
                break;
            }
        }

        if (keep) {
            results.push_back(candidate);
            if (results.size() >= TOP_K) {
                break;
            }
        }
    }

    return results;
}

static detection_t best_raw_candidate(dl::TensorBase *score0, dl::TensorBase *score1, dl::TensorBase *score2)
{
    int debug_class = -1;
    int debug_index = -1;
    float debug_score = 0.0f;
    float debug_raw = 0.0f;

    for (int i = 0; i < NUM_BOXES; ++i) {
        for (int c = 0; c < NUM_CLASSES; ++c) {
            const float raw = score_logit_value(score0, score1, score2, c, i);
            const float score = score_from_output(raw);
            if (score > debug_score) {
                debug_score = score;
                debug_raw = raw;
                debug_class = c;
                debug_index = i;
            }
        }
    }

    ESP_LOGI(TAG,
             "Best raw candidate: category=%d/%s, raw=%.3f, score=%.3f, index=%d",
             debug_class,
             debug_class >= 0 ? CLASS_NAMES[debug_class] : "none",
             debug_raw,
             debug_score,
             debug_index);

    return {
        debug_class,
        debug_score,
        0,
        0,
        0,
        0,
    };
}

extern "C" esp_err_t ai_submit_latest_frame(float total_weight_g, float item_weight_g)
{
    if (s_yolo_task_handle == nullptr || s_inference_queue == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    inference_request_t request = {
        .total_weight_g = total_weight_g,
        .item_weight_g = item_weight_g,
        .timestamp_us = esp_timer_get_time(),
    };

    if (xQueueSend(s_inference_queue, &request, 0) != pdPASS) {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

extern "C" int food_result_get_items(food_item_record_t *items, int max_items)
{
    if (!items || max_items <= 0 || !s_result_lock) {
        return 0;
    }
    if (xSemaphoreTake(s_result_lock, portMAX_DELAY) != pdPASS) {
        return 0;
    }

    const int copy_count = std::min(s_item_count, max_items);
    const int start = (s_item_count == FOOD_ITEM_BUFFER_LEN) ? s_item_write_index : 0;
    for (int i = 0; i < copy_count; ++i) {
        items[i] = s_item_buffer[(start + i) % FOOD_ITEM_BUFFER_LEN];
    }

    xSemaphoreGive(s_result_lock);
    return copy_count;
}

extern "C" int food_result_get_class_totals(food_class_total_t *totals, int max_totals)
{
    if (!totals || max_totals <= 0 || !s_result_lock) {
        return 0;
    }
    if (xSemaphoreTake(s_result_lock, portMAX_DELAY) != pdPASS) {
        return 0;
    }

    int out_count = 0;
    for (int i = 0; i < NUM_CLASSES && out_count < max_totals; ++i) {
        if (s_class_totals[i].count > 0) {
            totals[out_count++] = s_class_totals[i];
        }
    }

    xSemaphoreGive(s_result_lock);
    return out_count;
}

extern "C" void food_result_clear(void)
{
    if (!s_result_lock) {
        return;
    }
    if (xSemaphoreTake(s_result_lock, portMAX_DELAY) != pdPASS) {
        return;
    }

    std::memset(s_item_buffer, 0, sizeof(s_item_buffer));
    s_item_write_index = 0;
    s_item_count = 0;
    s_item_sequence = 0;
    for (int i = 0; i < NUM_CLASSES; ++i) {
        s_class_totals[i].count = 0;
        s_class_totals[i].total_weight_g = 0.0f;
    }

    xSemaphoreGive(s_result_lock);
}

extern "C" void ai_inference_task(void *arg)
{
    bool param_copy = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) >= (9 * 1024 * 1024);
    log_model_partition_header();
    dl::Model *model = new dl::Model(MODEL_PARTITION_LABEL,
                                     fbs::MODEL_LOCATION_IN_FLASH_PARTITION,
                                     0,
                                     dl::MEMORY_MANAGER_GREEDY,
                                     nullptr,
                                     param_copy);
    if (model == nullptr || model->get_inputs().empty()) {
        ESP_LOGE(TAG,
                 "YOLO model failed to load from partition '%s'; perception inference task will stop, UI remains usable",
                 MODEL_PARTITION_LABEL);
        delete model;
        s_yolo_task_handle = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    const std::string input_name = model->get_inputs().begin()->first;
    dl::TensorBase *model_input = model->get_input(input_name);
    if (model_input == nullptr) {
        ESP_LOGE(TAG, "YOLO model input '%s' is null; perception inference task will stop", input_name.c_str());
        delete model;
        s_yolo_task_handle = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    dl::image::ImagePreprocessor preprocessor(model, {0, 0, 0}, {255, 255, 255});
    preprocessor.enable_letterbox({114, 114, 114});
    init_result_store();
    s_inference_queue = xQueueCreate(INFERENCE_QUEUE_LEN, sizeof(inference_request_t));
    if (s_inference_queue == nullptr) {
        ESP_LOGE(TAG, "failed to create inference queue");
        vTaskDelete(NULL);
        return;
    }
    s_yolo_task_handle = xTaskGetCurrentTaskHandle();

    ESP_LOGI(TAG, "YOLO capture task started, waiting for trigger");

    while (1) {
        inference_request_t request = {};
        uint8_t *frame_buf = nullptr;
        uint32_t frame_size = 0;
        uint32_t frame_w = 0;
        uint32_t frame_h = 0;
        uint32_t frame_fmt = 0;

        if (xQueueReceive(s_inference_queue, &request, portMAX_DELAY) != pdPASS) {
            continue;
        }

        if (latest_frame_get(&frame_buf, &frame_size, &frame_w, &frame_h, &frame_fmt) != ESP_OK) {
            ESP_LOGW(TAG, "trigger received but no valid frame available");
            ai_scale_perception_mark_inference_idle();
            continue;
        }

        if (frame_fmt != V4L2_PIX_FMT_RGB565) {
            ESP_LOGW(TAG, "unsupported frame format=0x%" PRIx32 ", expected RGB565", frame_fmt);
            ai_scale_perception_mark_inference_idle();
            continue;
        }

        dl::image::img_t img = {
            .data = frame_buf,
            .width = static_cast<uint16_t>(frame_w),
            .height = static_cast<uint16_t>(frame_h),
            .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565LE,
        };

        const int64_t start_pre = esp_timer_get_time();
        preprocessor.preprocess(img);
        const int64_t end_pre = esp_timer_get_time();

        dl::TensorBase score0({1, GRID0, GRID0, NUM_CLASSES}, nullptr, 0, dl::DATA_TYPE_FLOAT);
        dl::TensorBase score1({1, GRID1, GRID1, NUM_CLASSES}, nullptr, 0, dl::DATA_TYPE_FLOAT);
        dl::TensorBase score2({1, GRID2, GRID2, NUM_CLASSES}, nullptr, 0, dl::DATA_TYPE_FLOAT);
        std::map<std::string, dl::TensorBase *> input_map = {{input_name, model_input}};
        std::map<std::string, dl::TensorBase *> debug_outputs = {
            {SCORE_OUTPUT_NAMES[0], &score0},
            {SCORE_OUTPUT_NAMES[1], &score1},
            {SCORE_OUTPUT_NAMES[2], &score2},
        };

        const int64_t start_inf = esp_timer_get_time();
        model->run(input_map, dl::RUNTIME_MODE_MULTI_CORE, debug_outputs);
        const int64_t end_inf = esp_timer_get_time();

        dl::TensorBase *output = model->get_output("output0");
        if (output == nullptr) {
            ESP_LOGE(TAG, "model output0 not found");
            ai_scale_perception_mark_inference_idle();
            continue;
        }

        const int64_t start_post = esp_timer_get_time();
        std::vector<detection_t> results =
            parse_yolo_output(output, &score0, &score1, &score2, preprocessor, img.width, img.height);
        const int64_t end_post = esp_timer_get_time();
        // ========== 新增：统计每个类别的数量 ==========
        std::map<int, int> class_count;
        for (const auto &res : results) {
            class_count[res.category]++;
        }
        detection_t best = results.empty() ? best_raw_candidate(&score0, &score1, &score2) : results.front();
        if (best.category < 0 || best.category >= NUM_CLASSES) {
            ESP_LOGW(TAG, "invalid best candidate; force class 0/%s", CLASS_NAMES[0]);
            best.category = 0;
            best.score = 0.0f;
        }
        store_food_item(request, best);
        ai_scale_perception_bridge_notify();
        ai_scale_perception_mark_inference_idle();
        // ============================================
        ESP_LOGI(TAG,
                 "Captured frame size=%" PRIu32 " | item %.2f g | total %.2f g | Pre: %lld ms | Inf: %lld ms | Post: %lld ms | detections: %u",
                 frame_size,
                 request.item_weight_g,
                 request.total_weight_g,
                 static_cast<long long>((end_pre - start_pre) / 1000),
                 static_cast<long long>((end_inf - start_inf) / 1000),
                 static_cast<long long>((end_post - start_post) / 1000),
                 static_cast<unsigned>(results.size()));
        ESP_LOGI(TAG,
                 "Stored item #%u: %s, item_weight=%.2f g, total_weight=%.2f g, score=%.3f",
                 s_item_sequence,
                 CLASS_NAMES[best.category],
                 request.item_weight_g,
                 request.total_weight_g,
                 best.score);
        // ========== 新增：打印每个类别的数量 ==========
        if (class_count.size() > 0) {
            ESP_LOGI(TAG, "=== Per-class count ===");
            for (const auto &[cat, count] : class_count) {
                ESP_LOGI(TAG, "  %s: %d", CLASS_NAMES[cat], count);
            }
        }
        // ============================================

        for (const auto &res : results) {
            ESP_LOGI(TAG,
                     "[category: %d/%s, score: %.3f, x1: %d, y1: %d, x2: %d, y2: %d]",
                     res.category,
                     CLASS_NAMES[res.category],
                     res.score,
                     res.x1,
                     res.y1,
                     res.x2,
                     res.y2);
        }
    }
}
