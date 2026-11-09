#include "camera_service.h"
#include "camera_runtime.h"
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_http_server.h>
#include <esp_timer.h>
#include <img_converters.h>
#include <cJSON.h>

namespace cameraService {
namespace {
constexpr size_t FRAME_BYTES = cfg::IMAGE_WIDTH * cfg::IMAGE_HEIGHT;
robot::CameraCalibration active, saved;
bool hasCalibration = false, hasSaved = false, restartRequired = false;
uint8_t* latestFrame = nullptr;
SemaphoreHandle_t frameMutex = nullptr;
uint32_t frameId = 0, session = 0;
int64_t frameTime = 0;

void headers(httpd_req_t* request) {
    httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "http://192.168.4.1");
    httpd_resp_set_hdr(
        request, "Access-Control-Expose-Headers", "X-Frame-Id, X-Frame-Age-Ms, X-Camera-Session");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
}

esp_err_t error(httpd_req_t* request, const char* status, const char* message) {
    headers(request);
    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "text/plain");
    return httpd_resp_send(request, message, HTTPD_RESP_USE_STRLEN);
}

cJSON* toJson(const robot::CameraCalibration& value) {
    cJSON* object = cJSON_CreateObject();
#define FIELD(name) cJSON_AddNumberToObject(object, #name, value.name)
    FIELD(version);
    FIELD(width);
    FIELD(height);
    FIELD(hmirror);
    FIELD(vflip);
    FIELD(fx);
    FIELD(fy);
    FIELD(cx);
    FIELD(cy);
    FIELD(k1);
    FIELD(k2);
    FIELD(p1);
    FIELD(p2);
    FIELD(k3);
    FIELD(rms);
#undef FIELD
    return object;
}

bool fromJson(const cJSON* object, robot::CameraCalibration& value) {
    if (!cJSON_IsObject(object))
        return false;
    // Check integer fields before conversion so fractions and negatives cannot wrap.
    const char* names[] = {"version", "width", "height", "hmirror", "vflip"};
    uint32_t* fields[] = {
        &value.version, &value.width, &value.height, &value.hmirror, &value.vflip};
    for (int i = 0; i < 5; ++i) {
        const cJSON* field = cJSON_GetObjectItemCaseSensitive(object, names[i]);
        if (!cJSON_IsNumber(field) || !isfinite(field->valuedouble) || field->valuedouble < 0 ||
            field->valuedouble > 10000 || floor(field->valuedouble) != field->valuedouble)
            return false;
        *fields[i] = uint32_t(field->valuedouble);
    }
    const char* floatNames[] = {"fx", "fy", "cx", "cy", "k1", "k2", "p1", "p2", "k3", "rms"};
    float* floatFields[] = {&value.fx,
                            &value.fy,
                            &value.cx,
                            &value.cy,
                            &value.k1,
                            &value.k2,
                            &value.p1,
                            &value.p2,
                            &value.k3,
                            &value.rms};
    for (int i = 0; i < 10; ++i) {
        const cJSON* field = cJSON_GetObjectItemCaseSensitive(object, floatNames[i]);
        if (!cJSON_IsNumber(field))
            return false;
        *floatFields[i] = field->valuedouble;
    }
    return value.valid();
}

esp_err_t settings(httpd_req_t* request) {
    cJSON* object = cJSON_CreateObject();
    cJSON_AddBoolToObject(object, "calibrated", hasCalibration);
    cJSON_AddNumberToObject(object, "bootId", session);
    cJSON_AddBoolToObject(object, "restart_required", restartRequired);
    cJSON_AddBoolToObject(object, "maintenance", cameraRuntime::maintenanceReady());
    cJSON_AddBoolToObject(object, "busy", cameraRuntime::busy());
    cJSON_AddItemToObject(object, "active", toJson(active));
    cJSON_AddItemToObject(object, "saved", hasSaved ? toJson(saved) : cJSON_CreateNull());
    char* json = cJSON_PrintUnformatted(object);
    cJSON_Delete(object);
    if (!json)
        return error(request, "503 Service Unavailable", "Not enough memory");
    headers(request);
    httpd_resp_set_type(request, "application/json");
    esp_err_t result = httpd_resp_send(request, json, HTTPD_RESP_USE_STRLEN);
    cJSON_free(json);
    return result;
}

esp_err_t saveSettings(httpd_req_t* request) {
    if (!cameraRuntime::maintenanceReady())
        return error(
            request, "409 Conflict", "Enable Updates & calibration in the robot app first");
    if (request->content_len == 0 || request->content_len > 1024)
        return error(request, "400 Bad Request", "Expected a calibration JSON object");
    char contentType[48] = {};
    if (httpd_req_get_hdr_value_str(request, "Content-Type", contentType, sizeof(contentType)) !=
            ESP_OK ||
        strcmp(contentType, "application/json") != 0)
        return error(request, "400 Bad Request", "Use Content-Type: application/json");
    char body[1025];
    size_t received = 0;
    const uint32_t started = millis();
    while (received < request->content_len) {
        if (millis() - started > 2000)
            return ESP_FAIL;
        int count = httpd_req_recv(request, body + received, request->content_len - received);
        if (count <= 0)
            return ESP_FAIL;
        received += count;
    }
    body[received] = '\0';
    cJSON* object = cJSON_ParseWithOpts(body, nullptr, true);
    robot::CameraCalibration candidate;
    const bool valid = fromJson(object, candidate);
    cJSON_Delete(object);
    if (!valid)
        return error(request, "400 Bad Request", "Invalid calibration, resolution or orientation");
    if (!cameraRuntime::beginWork())
        return error(
            request, "409 Conflict", "Maintenance unavailable or another write is in progress");

    struct ReleaseWork {
        ~ReleaseWork() {
            cameraRuntime::endWork();
        }
    } releaseWork;

    Preferences storage;
    if (!storage.begin("camera-cal", false))
        return error(request, "500 Internal Server Error", "Could not open flash storage");
    const size_t written = storage.putBytes("settings", &candidate, sizeof(candidate));
    robot::CameraCalibration readback;
    const bool verified =
        written == sizeof(candidate) &&
        storage.getBytes("settings", &readback, sizeof(readback)) == sizeof(readback) &&
        memcmp(&candidate, &readback, sizeof(candidate)) == 0;
    storage.end();
    if (!verified)
        return error(request, "500 Internal Server Error", "Could not save calibration");
    saved = candidate;
    hasSaved = true;
    restartRequired = true;
    return settings(request);
}

esp_err_t picture(httpd_req_t* request) {
    const bool raw = strcmp(request->uri, "/calibration/frame") == 0;
    if (raw && !cameraRuntime::maintenanceReady())
        return error(
            request, "409 Conflict", "Enable Updates & calibration before collecting images");
    if (!latestFrame || !frameMutex || cameraRuntime::busy())
        return error(request,
                     "503 Service Unavailable",
                     "Camera unavailable or firmware update in progress");
    uint8_t* copy = static_cast<uint8_t*>(ps_malloc(FRAME_BYTES));
    if (!copy)
        return error(request, "503 Service Unavailable", "Not enough image memory");
    uint32_t id;
    int64_t captured;
    if (xSemaphoreTake(frameMutex, pdMS_TO_TICKS(20)) != pdTRUE) {
        free(copy);
        return error(request, "503 Service Unavailable", "Camera busy");
    }
    id = frameId;
    captured = frameTime;
    if (captured > 0)
        memcpy(copy, latestFrame, FRAME_BYTES);
    xSemaphoreGive(frameMutex);
    if (!captured || esp_timer_get_time() - captured > 1500000) {
        free(copy);
        return error(request, "503 Service Unavailable", "No recent camera frame");
    }
    uint8_t* jpeg = nullptr;
    size_t length = 0;
    if (!raw && !fmt2jpg(copy,
                         FRAME_BYTES,
                         cfg::IMAGE_WIDTH,
                         cfg::IMAGE_HEIGHT,
                         PIXFORMAT_GRAYSCALE,
                         65,
                         &jpeg,
                         &length)) {
        free(copy);
        return error(request, "503 Service Unavailable", "JPEG conversion failed");
    }
    headers(request);
    char idText[16], ageText[24], sessionText[16];
    snprintf(idText, sizeof(idText), "%lu", static_cast<unsigned long>(id));
    snprintf(ageText, sizeof(ageText), "%lld", (esp_timer_get_time() - captured) / 1000);
    snprintf(sessionText, sizeof(sessionText), "%lu", static_cast<unsigned long>(session));
    httpd_resp_set_hdr(request, "X-Frame-Id", idText);
    httpd_resp_set_hdr(request, "X-Frame-Age-Ms", ageText);
    httpd_resp_set_hdr(request, "X-Camera-Session", sessionText);
    esp_err_t result;
    if (raw) {
        httpd_resp_set_type(request, "image/x-portable-graymap");
        const char pgm[] = "P5\n320 240\n255\n";
        result = httpd_resp_send_chunk(request, pgm, sizeof(pgm) - 1);
        if (result == ESP_OK)
            result = httpd_resp_send_chunk(request, reinterpret_cast<char*>(copy), FRAME_BYTES);
        if (result == ESP_OK)
            result = httpd_resp_send_chunk(request, nullptr, 0);
    } else {
        httpd_resp_set_type(request, "image/jpeg");
        result = httpd_resp_send(request, reinterpret_cast<char*>(jpeg), length);
    }
    free(jpeg);
    free(copy);
    return result;
}
} // namespace

const robot::CameraCalibration& calibration() {
    return active;
}

void begin() {
    Preferences storage;
    if (storage.begin("camera-cal", true)) {
        robot::CameraCalibration stored;
        if (storage.getBytesLength("settings") == sizeof(stored) &&
            storage.getBytes("settings", &stored, sizeof(stored)) == sizeof(stored) &&
            stored.valid()) {
            active = saved = stored;
            hasCalibration = hasSaved = true;
        }
        storage.end();
    }
    latestFrame = static_cast<uint8_t*>(ps_malloc(FRAME_BYTES));
    frameMutex = xSemaphoreCreateMutex();
    if (!latestFrame || !frameMutex) {
        free(latestFrame);
        latestFrame = nullptr;
        if (frameMutex)
            vSemaphoreDelete(frameMutex);
        frameMutex = nullptr;
    }
    session = esp_random();
    WiFi.mode(WIFI_STA);
    // The NodeMCU DHCP pool is .100 through .200. Reserve .250 for the camera.
    WiFi.config(
        IPAddress(192, 168, 4, 250), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    WiFi.setHostname("april-camera");
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);
    WiFi.begin(cfg::WIFI_SSID, cfg::WIFI_PASSWORD);
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.task_priority = 1; // Sensors/UART have priority 2 on this core.
    config.core_id = 0;
    config.stack_size = 8192;
    config.max_open_sockets = 3;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 1;
    config.send_wait_timeout = 1;
    httpd_handle_t server = nullptr;
    if (httpd_start(&server, &config) != ESP_OK)
        return;
    httpd_uri_t routes[] = {
        {"/frame.jpg", HTTP_GET, picture, nullptr},
        {"/calibration/frame", HTTP_GET, picture, nullptr},
        {"/calibration", HTTP_GET, settings, nullptr},
        {"/calibration", HTTP_POST, saveSettings, nullptr},
    };
    for (const auto& route : routes)
        httpd_register_uri_handler(server, &route);
}

void publishFrame(const camera_fb_t* frame, uint32_t sequence, int64_t captureUs) {
    if (!latestFrame || !frameMutex || xSemaphoreTake(frameMutex, 0) != pdTRUE)
        return;
    memcpy(latestFrame, frame->buf, FRAME_BYTES);
    frameId = sequence;
    frameTime = captureUs;
    xSemaphoreGive(frameMutex);
}
} // namespace cameraService
