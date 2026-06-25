#include "ai_scale_perception.h"
#include "perception_internal.h"
#include "food_result.h"
#include "hx711.h"

#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <unistd.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "example_video_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "linux/videodev2.h"
#include "sdkconfig.h"

#define PERCEPTION_CAMERA_VIDEO_BUFFER_NUMBER CONFIG_AI_SCALE_CAMERA_VIDEO_BUFFER_NUMBER
#define PERCEPTION_HX711_DATA_GPIO            GPIO_NUM_22
#define PERCEPTION_HX711_SCK_GPIO             GPIO_NUM_23
#define PERCEPTION_HX711_SCALE                CONFIG_AI_SCALE_HX711_SCALE
#define PERCEPTION_HX711_AVG_SAMPLES          CONFIG_AI_SCALE_HX711_AVG_SAMPLES
#define PERCEPTION_WEIGHT_POLL_INTERVAL_MS    CONFIG_AI_SCALE_WEIGHT_POLL_INTERVAL_MS
#define PERCEPTION_WEIGHT_CHANGE_THRESHOLD_G  CONFIG_AI_SCALE_WEIGHT_CHANGE_THRESHOLD_G
#define PERCEPTION_WEIGHT_STABLE_DELTA_G      CONFIG_AI_SCALE_WEIGHT_STABLE_DELTA_G
#define PERCEPTION_WEIGHT_STABLE_COUNT        CONFIG_AI_SCALE_WEIGHT_STABLE_COUNT
#define PERCEPTION_TRIGGER_COOLDOWN_MS        CONFIG_AI_SCALE_TRIGGER_COOLDOWN_MS
#define PERCEPTION_AI_CAPTURE_TIMEOUT_MS      CONFIG_AI_SCALE_AI_CAPTURE_TIMEOUT_MS
#define PERCEPTION_AI_TASK_WDT_TIMEOUT_MS     20000
#define PERCEPTION_EMPTY_WEIGHT_G             8.0f

typedef struct {
    int fd;
    uint8_t *buffer[PERCEPTION_CAMERA_VIDEO_BUFFER_NUMBER];
    uint32_t buffer_size;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;
} perception_video_t;

typedef struct {
    uint8_t *buf;
    uint32_t capacity;
    uint32_t size;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;
    bool valid;
    SemaphoreHandle_t lock;
} latest_frame_buffer_t;

static const char *TAG = "perception_camera";
static latest_frame_buffer_t s_latest_frame = {};
static perception_video_t s_video = {.fd = -1};
static bool s_video_ready;
static bool s_weight_task_started;
static bool s_yolo_task_started;
static volatile bool s_inference_busy;
static TaskHandle_t s_yolo_task_handle;

static float absf_local(float value)
{
    return value < 0.0f ? -value : value;
}

static float normalize_scale_weight(float weight_g)
{
    return absf_local(weight_g) <= PERCEPTION_EMPTY_WEIGHT_G ? 0.0f : weight_g;
}

static esp_err_t latest_frame_init(const perception_video_t *video)
{
    if (s_latest_frame.buf) {
        return ESP_OK;
    }

    s_latest_frame.capacity = video->buffer_size;
    s_latest_frame.buf = heap_caps_malloc(s_latest_frame.capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(s_latest_frame.buf, ESP_ERR_NO_MEM, TAG, "failed to allocate latest frame");

    s_latest_frame.lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_latest_frame.lock, ESP_ERR_NO_MEM, TAG, "failed to create frame lock");

    s_latest_frame.width = video->width;
    s_latest_frame.height = video->height;
    s_latest_frame.pixel_format = video->pixel_format;
    ESP_LOGI(TAG, "latest frame buffer: %" PRIu32 " bytes, %" PRIu32 "x%" PRIu32,
             s_latest_frame.capacity, video->width, video->height);
    return ESP_OK;
}

static esp_err_t latest_frame_update(const perception_video_t *video, const struct v4l2_buffer *buf)
{
    ESP_RETURN_ON_FALSE(s_latest_frame.buf, ESP_ERR_INVALID_STATE, TAG, "latest frame not initialized");
    ESP_RETURN_ON_FALSE(buf->bytesused <= s_latest_frame.capacity, ESP_ERR_NO_MEM, TAG, "frame too large");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(s_latest_frame.lock, portMAX_DELAY) == pdPASS,
                        ESP_FAIL, TAG, "failed to take frame lock");

    memcpy(s_latest_frame.buf, video->buffer[buf->index], buf->bytesused);
    s_latest_frame.size = buf->bytesused;
    s_latest_frame.width = video->width;
    s_latest_frame.height = video->height;
    s_latest_frame.pixel_format = video->pixel_format;
    s_latest_frame.valid = true;

    xSemaphoreGive(s_latest_frame.lock);
    return ESP_OK;
}

esp_err_t latest_frame_get(uint8_t **buf, uint32_t *size, uint32_t *width,
                           uint32_t *height, uint32_t *pixel_format)
{
    ESP_RETURN_ON_FALSE(buf && size && width && height && pixel_format,
                        ESP_ERR_INVALID_ARG, TAG, "invalid frame getter args");
    ESP_RETURN_ON_FALSE(s_latest_frame.valid, ESP_ERR_NOT_FOUND, TAG, "no valid latest frame");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(s_latest_frame.lock, portMAX_DELAY) == pdPASS,
                        ESP_FAIL, TAG, "failed to take frame lock");

    *buf = s_latest_frame.buf;
    *size = s_latest_frame.size;
    *width = s_latest_frame.width;
    *height = s_latest_frame.height;
    *pixel_format = s_latest_frame.pixel_format;

    xSemaphoreGive(s_latest_frame.lock);
    return ESP_OK;
}

static esp_err_t init_video_device(const char *dev_name, perception_video_t *video)
{
    struct v4l2_format format;
    struct v4l2_requestbuffers req;
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    esp_err_t ret = ESP_FAIL;

    video->fd = open(dev_name, O_RDWR);
    ESP_RETURN_ON_FALSE(video->fd >= 0, ESP_ERR_NOT_FOUND, TAG, "open %s failed", dev_name);

    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ESP_GOTO_ON_ERROR(ioctl(video->fd, VIDIOC_G_FMT, &format), fail, TAG, "get format failed");

#if CONFIG_ESP_VIDEO_ENABLE_SWAP_BYTE
    if (format.fmt.pix.pixelformat == V4L2_PIX_FMT_RGB565X) {
        format.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
        ESP_GOTO_ON_ERROR(ioctl(video->fd, VIDIOC_S_FMT, &format), fail, TAG, "set RGB565 failed");
    }
#endif

    ESP_GOTO_ON_FALSE(format.fmt.pix.pixelformat == V4L2_PIX_FMT_RGB565,
                      ESP_ERR_NOT_SUPPORTED, fail, TAG, "AI camera must output RGB565");

    memset(&req, 0, sizeof(req));
    req.count = PERCEPTION_CAMERA_VIDEO_BUFFER_NUMBER;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    ESP_GOTO_ON_ERROR(ioctl(video->fd, VIDIOC_REQBUFS, &req), fail, TAG, "request buffers failed");

    for (int i = 0; i < PERCEPTION_CAMERA_VIDEO_BUFFER_NUMBER; ++i) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        ESP_GOTO_ON_ERROR(ioctl(video->fd, VIDIOC_QUERYBUF, &buf), fail, TAG, "query buffer failed");

        video->buffer[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, video->fd, buf.m.offset);
        ESP_GOTO_ON_FALSE(video->buffer[i] != MAP_FAILED, ESP_ERR_NO_MEM, fail, TAG, "mmap failed");
        video->buffer_size = buf.length;
        ESP_GOTO_ON_ERROR(ioctl(video->fd, VIDIOC_QBUF, &buf), fail, TAG, "queue buffer failed");
    }

    video->width = format.fmt.pix.width;
    video->height = format.fmt.pix.height;
    video->pixel_format = format.fmt.pix.pixelformat;

    ESP_GOTO_ON_ERROR(latest_frame_init(video), fail, TAG, "latest frame init failed");
    ESP_GOTO_ON_ERROR(ioctl(video->fd, VIDIOC_STREAMON, &type), fail, TAG, "stream on failed");

    ESP_LOGI(TAG, "camera ready: %s, %" PRIu32 "x%" PRIu32 " RGB565", dev_name, video->width, video->height);
    return ESP_OK;

fail:
    if (video->fd >= 0) {
        close(video->fd);
    }
    video->fd = -1;
    return ret;
}

static esp_err_t capture_frame_to_latest_buffer(void)
{
    struct timeval timeout = {
        .tv_sec = PERCEPTION_AI_CAPTURE_TIMEOUT_MS / 1000,
        .tv_usec = (PERCEPTION_AI_CAPTURE_TIMEOUT_MS % 1000) * 1000,
    };
    fd_set read_fds;
    struct v4l2_buffer buf;
    esp_err_t ret;

    ESP_RETURN_ON_FALSE(s_video_ready, ESP_ERR_INVALID_STATE, TAG, "camera is not ready");

    FD_ZERO(&read_fds);
    FD_SET(s_video.fd, &read_fds);
    ESP_RETURN_ON_FALSE(select(s_video.fd + 1, &read_fds, NULL, NULL, &timeout) > 0,
                        ESP_ERR_TIMEOUT, TAG, "capture timeout");

    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    ESP_RETURN_ON_ERROR(ioctl(s_video.fd, VIDIOC_DQBUF, &buf), TAG, "dequeue frame failed");
    if (!(buf.flags & V4L2_BUF_FLAG_DONE)) {
        ioctl(s_video.fd, VIDIOC_QBUF, &buf);
        return ESP_ERR_INVALID_RESPONSE;
    }

    ret = latest_frame_update(&s_video, &buf);
    ioctl(s_video.fd, VIDIOC_QBUF, &buf);
    return ret;
}

static esp_err_t trigger_capture_from_weight(float stable_weight_g, float delta_g)
{
    stable_weight_g = normalize_scale_weight(stable_weight_g);

    if (stable_weight_g <= 0.0f || delta_g < 0.0f) {
        food_result_apply_weight_delta(stable_weight_g, delta_g);
        ai_scale_perception_bridge_notify();
        return ESP_OK;
    }

    if (s_inference_busy) {
        ESP_LOGI(TAG, "YOLO busy; skip this stable weight event");
        return ESP_ERR_TIMEOUT;
    }
    s_inference_busy = true;

    esp_err_t ret = capture_frame_to_latest_buffer();
    if (ret != ESP_OK) {
        s_inference_busy = false;
        return ret;
    }

    ret = ai_submit_latest_frame(stable_weight_g, delta_g);
    if (ret != ESP_OK) {
        s_inference_busy = false;
    }
    return ret;
}

void ai_scale_perception_mark_inference_idle(void)
{
    s_inference_busy = false;
}

static void weight_capture_trigger_task(void *arg)
{
    (void)arg;
    float reference_weight_g = 0.0f;
    float candidate_weight_g = 0.0f;
    int stable_count = 0;
    bool change_detected = false;
    int64_t last_trigger_time_us = 0;

    hx711_init(PERCEPTION_HX711_DATA_GPIO, PERCEPTION_HX711_SCK_GPIO, HX711_GAIN_128);
    hx711_set_scale((float)PERCEPTION_HX711_SCALE);
    vTaskDelay(pdMS_TO_TICKS(1000));
    hx711_tare();
    vTaskDelay(pdMS_TO_TICKS(300));

    reference_weight_g = normalize_scale_weight(hx711_get_units(PERCEPTION_HX711_AVG_SAMPLES));
    candidate_weight_g = reference_weight_g;
    stable_count = PERCEPTION_WEIGHT_STABLE_COUNT;

    ESP_LOGI(TAG, "HX711 ready DOUT=%d SCK=%d, reference %.1f g",
             PERCEPTION_HX711_DATA_GPIO, PERCEPTION_HX711_SCK_GPIO, reference_weight_g);

    while (true) {
        const float current_weight_g = normalize_scale_weight(hx711_get_units(PERCEPTION_HX711_AVG_SAMPLES));
        const float candidate_delta_g = absf_local(current_weight_g - candidate_weight_g);
        const float reference_delta_g = absf_local(current_weight_g - reference_weight_g);

        if (candidate_delta_g <= PERCEPTION_WEIGHT_STABLE_DELTA_G) {
            candidate_weight_g = (candidate_weight_g * stable_count + current_weight_g) / (stable_count + 1);
            if (stable_count < PERCEPTION_WEIGHT_STABLE_COUNT) {
                ++stable_count;
            }
        } else {
            candidate_weight_g = current_weight_g;
            stable_count = 1;
        }

        if (!change_detected && reference_delta_g >= PERCEPTION_WEIGHT_CHANGE_THRESHOLD_G) {
            change_detected = true;
            stable_count = 1;
            candidate_weight_g = current_weight_g;
        }

        if (change_detected && stable_count >= PERCEPTION_WEIGHT_STABLE_COUNT) {
            const int64_t now_us = esp_timer_get_time();
            const float signed_delta_g = candidate_weight_g - reference_weight_g;
            const float stable_delta_g = absf_local(signed_delta_g);

            if (stable_delta_g >= PERCEPTION_WEIGHT_CHANGE_THRESHOLD_G &&
                (now_us - last_trigger_time_us) >= (PERCEPTION_TRIGGER_COOLDOWN_MS * 1000LL)) {
                if (trigger_capture_from_weight(candidate_weight_g, signed_delta_g) == ESP_OK) {
                    last_trigger_time_us = now_us;
                    reference_weight_g = normalize_scale_weight(candidate_weight_g);
                }
            }

            change_detected = false;
        }

        vTaskDelay(pdMS_TO_TICKS(PERCEPTION_WEIGHT_POLL_INTERVAL_MS));
    }
}

esp_err_t ai_scale_perception_camera_start_pipeline(void)
{
    if (s_video_ready) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(example_video_init(), TAG, "video init failed");

    const char *devices[] = {
#if EXAMPLE_ENABLE_MIPI_CSI_CAM_SENSOR
        ESP_VIDEO_MIPI_CSI_DEVICE_NAME,
#endif
#if EXAMPLE_ENABLE_DVP_CAM_SENSOR
        ESP_VIDEO_DVP_DEVICE_NAME,
#endif
#if EXAMPLE_ENABLE_SPI_CAM_0_SENSOR
        ESP_VIDEO_SPI_DEVICE_NAME,
#endif
#if EXAMPLE_ENABLE_USB_UVC_CAM_SENSOR
        ESP_VIDEO_USB_UVC_DEVICE_NAME(0),
#endif
    };

    for (size_t i = 0; i < sizeof(devices) / sizeof(devices[0]); ++i) {
        if (init_video_device(devices[i], &s_video) == ESP_OK) {
            s_video_ready = true;
            break;
        }
    }

    ESP_RETURN_ON_FALSE(s_video_ready, ESP_ERR_NOT_FOUND, TAG, "no RGB565 camera found");

    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = PERCEPTION_AI_TASK_WDT_TIMEOUT_MS,
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
        .trigger_panic = true,
    };
    esp_task_wdt_reconfigure(&twdt_config);

    if (!s_yolo_task_started) {
        BaseType_t ok = xTaskCreatePinnedToCore(ai_inference_task,
                                                "food_yolo",
                                                16 * 1024,
                                                NULL,
                                                5,
                                                &s_yolo_task_handle,
                                                1);
        ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "create YOLO task failed");
        s_yolo_task_started = true;
    }

    if (!s_weight_task_started) {
        BaseType_t ok = xTaskCreate(weight_capture_trigger_task,
                                    "weight_sensor",
                                    4096,
                                    NULL,
                                    3,
                                    NULL);
        ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "create weight task failed");
        s_weight_task_started = true;
    }

    return ESP_OK;
}
