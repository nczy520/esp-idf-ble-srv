#include "ble_srv_ota_bt.h"
#include "ble_srv_ota_common.h"
#include "ble_srv_gatt.h"
#include "ble_srv_msg.h"
#include "ble_srv.h"
#include "ble_srv_log.h"
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_heap_caps.h"

static const char *TAG = "OTA_BT";

#define BLE_SRV_WRITE_BUF_SIZE      (64 * 1024)
#define BLE_SRV_ACK_BATCH           12

static const uint32_t s_crc32_table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
    0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988, 0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
    0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
    0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172, 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
    0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
    0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, 0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
    0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
    0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E, 0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
    0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
    0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0, 0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
    0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
    0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A, 0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
    0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
    0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC, 0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
    0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
    0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236, 0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
    0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
    0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38, 0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
    0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
    0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2, 0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
    0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
    0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94, 0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D,
};

static inline uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        crc = s_crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc;
}

static bool s_initialized = false;

static SemaphoreHandle_t s_bt_lock = NULL;

#define BT_LOCK()   xSemaphoreTakeRecursive(s_bt_lock, portMAX_DELAY)
#define BT_UNLOCK() xSemaphoreGiveRecursive(s_bt_lock)

static volatile uint8_t s_gen = BLE_OTA_INVALID_GEN;
static volatile esp_ota_handle_t s_ota_handle = 0;
static const esp_partition_t *s_target_partition = NULL;
static volatile uint32_t s_fw_total_size = 0;
static volatile uint32_t s_fw_crc = 0;
static volatile uint32_t s_running_crc = 0xFFFFFFFF;
static volatile uint32_t s_fw_bytes_written = 0;
static volatile uint32_t s_total_received = 0;

static uint8_t *s_write_buf = NULL;
static volatile uint32_t s_write_buf_len = 0;
static volatile uint16_t s_packet_count = 0;
static volatile bool s_receiving = false;

static void bt_cleanup(void)
{
    s_receiving = false;
    s_packet_count = 0;

    if (s_ota_handle != 0) {
        esp_ota_abort(s_ota_handle);
        s_ota_handle = 0;
    }

    s_target_partition = NULL;
    s_write_buf_len = 0;
    s_fw_total_size = 0;
    s_fw_crc = 0;
    s_running_crc = 0xFFFFFFFF;
    s_fw_bytes_written = 0;
    s_total_received = 0;
}

static void bt_flush_and_finish(uint8_t gen, ble_ota_state_t result, ble_ota_err_t error)
{
    bt_cleanup();
    s_gen = BLE_OTA_INVALID_GEN;
    ble_srv_ota_finish(gen, result, error);
}

void ble_srv_ota_bt_handle_abort(void)
{
    BT_LOCK();
    uint8_t gen = s_gen;
    bool is_bt_mode = (ble_srv_ota_get_mode() == BLE_OTA_MODE_BT);
    bool gen_valid = (gen != BLE_OTA_INVALID_GEN) && ble_srv_ota_gen_valid(gen);

    if (is_bt_mode && gen_valid) {
        BLE_SRV_LOGW(TAG, "OTA abort, gen=%u", gen);
        bt_flush_and_finish(gen, BLE_OTA_STATE_ABORTED, BLE_OTA_ERR_ABORTED);
    }
    BT_UNLOCK();
}

bool ble_srv_ota_bt_init(void)
{
    if (s_initialized) {
        BLE_SRV_LOGW(TAG, "BT OTA already initialized");
        return true;
    }

    s_bt_lock = xSemaphoreCreateRecursiveMutex();
    if (!s_bt_lock) {
        BLE_SRV_LOGE(TAG, "Failed to create BT OTA lock");
        return false;
    }

    s_write_buf = heap_caps_malloc(BLE_SRV_WRITE_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_write_buf) {
        BLE_SRV_LOGW(TAG, "PSRAM alloc failed for %d bytes, trying internal RAM", BLE_SRV_WRITE_BUF_SIZE);
        s_write_buf = heap_caps_malloc(BLE_SRV_WRITE_BUF_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!s_write_buf) {
        BLE_SRV_LOGE(TAG, "Failed to allocate OTA write buffer (%d bytes)", BLE_SRV_WRITE_BUF_SIZE);
        vSemaphoreDelete(s_bt_lock);
        s_bt_lock = NULL;
        return false;
    }

    s_receiving = false;
    s_gen = BLE_OTA_INVALID_GEN;
    s_ota_handle = 0;
    s_target_partition = NULL;
    s_write_buf_len = 0;
    s_fw_total_size = 0;
    s_fw_crc = 0;
    s_running_crc = 0xFFFFFFFF;
    s_fw_bytes_written = 0;
    s_total_received = 0;
    s_packet_count = 0;

    s_initialized = true;
    BLE_SRV_LOGI(TAG, "BT OTA initialized, write buffer=%d bytes", BLE_SRV_WRITE_BUF_SIZE);
    return true;
}

void ble_srv_ota_bt_deinit(void)
{
    if (!s_initialized) {
        return;
    }

    if (ble_srv_ota_get_mode() == BLE_OTA_MODE_BT) {
        ble_srv_ota_abort(BLE_OTA_ERR_ABORTED);
    }

    BT_LOCK();
    bt_cleanup();
    s_gen = BLE_OTA_INVALID_GEN;
    BT_UNLOCK();

    if (s_write_buf) {
        heap_caps_free(s_write_buf);
        s_write_buf = NULL;
    }

    s_initialized = false;

    if (s_bt_lock) {
        vSemaphoreDelete(s_bt_lock);
        s_bt_lock = NULL;
    }
}

static bool handle_start(const uint8_t *data, uint16_t len)
{
    if (len < sizeof(ble_ota_bt_start_req_t)) {
        BLE_SRV_LOGE(TAG, "START payload too short: %u < %u", len, (unsigned)sizeof(ble_ota_bt_start_req_t));
        return false;
    }

    uint8_t gen = ble_srv_ota_begin(BLE_OTA_MODE_BT);
    if (gen == BLE_OTA_INVALID_GEN) {
        BLE_SRV_LOGE(TAG, "Cannot begin BT OTA, session busy");
        return false;
    }

    s_gen = gen;

    // 使用 memcpy 避免 uint8_t* 到结构体指针的严格别名违规与对齐风险
    ble_ota_bt_start_req_t req;
    memcpy(&req, data, sizeof(req));
    s_fw_total_size = req.fw_size;
    s_fw_crc = req.fw_crc;
    s_running_crc = 0xFFFFFFFF;

    BLE_SRV_LOGI(TAG, "OTA start: size=%lu, crc=0x%08lX, gen=%u",
                 (unsigned long)s_fw_total_size, (unsigned long)s_fw_crc, gen);

    if (s_fw_total_size == 0 || s_fw_total_size > BLE_OTA_MAX_FW_SIZE) {
        BLE_SRV_LOGE(TAG, "Start fail: invalid size %lu", (unsigned long)s_fw_total_size);
        bt_cleanup();
        s_gen = BLE_OTA_INVALID_GEN;
        ble_srv_ota_finish(gen, BLE_OTA_STATE_ERROR, BLE_OTA_ERR_INVALID_SIZE);
        return false;
    }

    s_target_partition = esp_ota_get_next_update_partition(NULL);
    if (!s_target_partition) {
        BLE_SRV_LOGE(TAG, "Start fail: no update partition");
        bt_cleanup();
        s_gen = BLE_OTA_INVALID_GEN;
        ble_srv_ota_finish(gen, BLE_OTA_STATE_ERROR, BLE_OTA_ERR_NO_PARTITION);
        return false;
    }

    BLE_SRV_LOGI(TAG, "Target: %s @0x%lx (%lu bytes)",
             s_target_partition->label,
             (unsigned long)s_target_partition->address,
             (unsigned long)s_target_partition->size);

    if (s_ota_handle != 0) {
        esp_ota_abort(s_ota_handle);
        s_ota_handle = 0;
    }

    s_write_buf_len = 0;
    s_fw_bytes_written = 0;
    s_total_received = 0;
    s_running_crc = 0xFFFFFFFF;
    s_packet_count = 0;

    // s_ota_handle 为 volatile，需用非 volatile 临时变量接收 esp_ota_begin 输出
    esp_ota_handle_t ota_handle = 0;
    esp_err_t ret = esp_ota_begin(s_target_partition, s_fw_total_size, &ota_handle);
    s_ota_handle = ota_handle;
    if (ret != ESP_OK) {
        BLE_SRV_LOGE(TAG, "Start fail: ota_begin %s", esp_err_to_name(ret));
        bt_cleanup();
        s_gen = BLE_OTA_INVALID_GEN;
        ble_srv_ota_finish(gen, BLE_OTA_STATE_ERROR, BLE_OTA_ERR_INTERNAL);
        return false;
    }

    s_receiving = true;
    uint32_t fw_size = s_fw_total_size;

    ble_srv_ota_set_fw_size(gen, fw_size);
    ble_srv_ota_report_progress(gen, 0, 0);
    ble_srv_ota_set_state(gen, BLE_OTA_STATE_RECEIVING, BLE_OTA_ERR_NONE);

    BLE_SRV_LOGI(TAG, "BT OTA session started");
    return true;
}

static bool handle_verify(void)
{
    uint8_t gen = s_gen;
    bool valid = (gen != BLE_OTA_INVALID_GEN) && ble_srv_ota_gen_valid(gen) &&
                 (ble_srv_ota_get_mode() == BLE_OTA_MODE_BT) && s_receiving;
    if (!valid) {
        BLE_SRV_LOGW(TAG, "VERIFY ignored: no BT OTA session");
        return false;
    }

    if (ble_srv_ota_get_state() != BLE_OTA_STATE_RECEIVING) {
        BLE_SRV_LOGW(TAG, "VERIFY ignored, state=%d", ble_srv_ota_get_state());
        return false;
    }

    s_receiving = false;

    if (s_write_buf_len > 0) {
        esp_err_t ret = esp_ota_write(s_ota_handle, s_write_buf, s_write_buf_len);
        if (ret != ESP_OK) {
            BLE_SRV_LOGE(TAG, "OTA flush fail: %s", esp_err_to_name(ret));
            bt_cleanup();
            s_gen = BLE_OTA_INVALID_GEN;
            ble_srv_ota_finish(gen, BLE_OTA_STATE_ERROR, BLE_OTA_ERR_FLASH_WRITE);
            return false;
        }
        s_fw_bytes_written += s_write_buf_len;
        s_write_buf_len = 0;
    }

    uint32_t bytes_written = s_fw_bytes_written;
    uint32_t total_received = s_total_received;
    uint32_t fw_total = s_fw_total_size;
    uint32_t expected_crc = s_fw_crc;
    uint32_t computed_crc = s_running_crc ^ 0xFFFFFFFF;
    esp_ota_handle_t handle = s_ota_handle;

    ble_srv_ota_report_progress(gen, total_received, bytes_written);

    BLE_SRV_LOGI(TAG, "Flushed: written=%lu, received=%lu",
             (unsigned long)bytes_written, (unsigned long)total_received);

    if (bytes_written != fw_total) {
        BLE_SRV_LOGE(TAG, "Verify fail: size mismatch %lu != %lu",
                     (unsigned long)bytes_written, (unsigned long)fw_total);
        bt_flush_and_finish(gen, BLE_OTA_STATE_ERROR, BLE_OTA_ERR_INVALID_SIZE);
        return false;
    }

    BLE_SRV_LOGI(TAG, "CRC check: expected=0x%08lX, computed=0x%08lX",
             (unsigned long)expected_crc, (unsigned long)computed_crc);
    if (computed_crc != expected_crc) {
        BLE_SRV_LOGE(TAG, "Verify fail: CRC mismatch exp=0x%08lX got=0x%08lX",
                     (unsigned long)expected_crc, (unsigned long)computed_crc);
        bt_flush_and_finish(gen, BLE_OTA_STATE_VERIFY_FAIL, BLE_OTA_ERR_CRC_MISMATCH);
        return false;
    }

    ble_srv_ota_set_state(gen, BLE_OTA_STATE_VERIFYING, BLE_OTA_ERR_NONE);

    esp_err_t ret = esp_ota_end(handle);
    s_ota_handle = 0;

    if (ret != ESP_OK) {
        BLE_SRV_LOGE(TAG, "Verify fail: ota_end %s", esp_err_to_name(ret));
        bt_flush_and_finish(gen, BLE_OTA_STATE_VERIFY_FAIL, BLE_OTA_ERR_VERIFY_FAILED);
        return false;
    }

    s_write_buf_len = 0;

    ble_srv_ota_report_progress(gen, total_received, bytes_written);
    ble_srv_ota_set_state(gen, BLE_OTA_STATE_VERIFY_OK, BLE_OTA_ERR_NONE);
    BLE_SRV_LOGI(TAG, "Verify OK");
    return true;
}

static bool handle_apply(void)
{
    uint8_t gen = s_gen;
    bool valid = (gen != BLE_OTA_INVALID_GEN) && ble_srv_ota_gen_valid(gen) &&
                 (ble_srv_ota_get_mode() == BLE_OTA_MODE_BT);
    const esp_partition_t *part = s_target_partition;

    if (!valid) {
        BLE_SRV_LOGW(TAG, "APPLY ignored: no BT OTA session");
        return false;
    }

    if (ble_srv_ota_get_state() != BLE_OTA_STATE_VERIFY_OK) {
        BLE_SRV_LOGW(TAG, "APPLY ignored, state=%d", ble_srv_ota_get_state());
        return false;
    }

    if (!part) {
        bt_flush_and_finish(gen, BLE_OTA_STATE_ERROR, BLE_OTA_ERR_NO_PARTITION);
        return false;
    }

    BLE_SRV_LOGI(TAG, "APPLY: boot -> %s @0x%lx",
             part->label, (unsigned long)part->address);

    ble_srv_ota_set_state(gen, BLE_OTA_STATE_APPLYING, BLE_OTA_ERR_NONE);

    esp_err_t ret = esp_ota_set_boot_partition(part);
    if (ret != ESP_OK) {
        BLE_SRV_LOGE(TAG, "Apply fail: set boot %s", esp_err_to_name(ret));
        bt_flush_and_finish(gen, BLE_OTA_STATE_APPLY_FAIL, BLE_OTA_ERR_INTERNAL);
        return false;
    }

    const esp_partition_t *boot = esp_ota_get_boot_partition();

    BLE_SRV_LOGI(TAG, "Apply OK: %s", boot ? boot->label : "?");
    bt_flush_and_finish(gen, BLE_OTA_STATE_APPLY_OK, BLE_OTA_ERR_NONE);
    return true;
}

bool ble_srv_ota_bt_dispatch_cmd(const uint8_t *data, uint16_t len)
{
    if (!data || len == 0) {
        BLE_SRV_LOGW(TAG, "Empty command ignored");
        return false;
    }

    BT_LOCK();
    bool result = false;

    ble_ota_bt_cmd_t cmd = (ble_ota_bt_cmd_t)data[0];
    uint16_t payload_len = len - 1;
    const uint8_t *payload = data + 1;

    BLE_SRV_LOGI(TAG, "BT OTA cmd=0x%02X payload=%u state=%d",
             cmd, payload_len, ble_srv_ota_get_state());

    switch (cmd) {
    case BLE_OTA_BT_CMD_START:
        result = handle_start(payload, payload_len);
        break;
    case BLE_OTA_BT_CMD_ABORT:
        if (ble_srv_ota_get_mode() == BLE_OTA_MODE_BT) {
            ble_srv_ota_abort(BLE_OTA_ERR_ABORTED);
            result = true;
        }
        break;
    case BLE_OTA_BT_CMD_VERIFY:
        result = handle_verify();
        break;
    case BLE_OTA_BT_CMD_APPLY:
        result = handle_apply();
        break;
    default:
        BLE_SRV_LOGW(TAG, "Unknown cmd: 0x%02X", cmd);
        break;
    }

    BT_UNLOCK();
    return result;
}

static bool process_fw_data_locked(const uint8_t *data, uint16_t len)
{
    uint8_t gen = s_gen;
    if (gen == BLE_OTA_INVALID_GEN || !ble_srv_ota_gen_valid(gen)) {
        return false;
    }

    if (!s_receiving) {
        return false;
    }

    if (ble_srv_ota_get_state() != BLE_OTA_STATE_RECEIVING) {
        return false;
    }

    if (ble_srv_ota_is_abort_requested()) {
        return false;
    }

    if (!data || len < 4) {
        BLE_SRV_LOGE(TAG, "Invalid fw_data: data=%p len=%u", data, len);
        bt_cleanup();
        s_gen = BLE_OTA_INVALID_GEN;
        ble_srv_ota_finish(gen, BLE_OTA_STATE_ERROR, BLE_OTA_ERR_INTERNAL);
        return false;
    }

    uint32_t offset = 0;
    offset |= (uint32_t)data[0] << 0;
    offset |= (uint32_t)data[1] << 8;
    offset |= (uint32_t)data[2] << 16;
    offset |= (uint32_t)data[3] << 24;
    const uint8_t *payload = data + 4;
    uint16_t payload_len = len - 4;

    if (offset > s_total_received) {
        BLE_SRV_LOGW(TAG, "Out-of-order data: got=%lu exp=%lu",
                     (unsigned long)offset, (unsigned long)s_total_received);
        uint32_t recv = s_total_received;
        uint32_t written = s_fw_bytes_written;
        if (ble_srv_ota_gen_valid(gen)) {
            ble_srv_ota_report_progress(gen, recv, written);
            ble_srv_ota_push_status(gen);
        }
        return true;
    }

    if (offset < s_total_received) {
        // 重复或乱序旧包：去重并丢弃。
        // 进一步可校验 offset + payload_len 是否仍在窗口内，避免重叠包污染。
        // 当前协议为严格顺序推进，重复包直接忽略即可。
        BLE_SRV_LOGD(TAG, "Duplicate/old packet ignored: got=%lu exp=%lu",
                     (unsigned long)offset, (unsigned long)s_total_received);
        return true;
    }

    if (payload_len == 0) {
        return true;
    }

    if (s_total_received + payload_len > s_fw_total_size) {
        BLE_SRV_LOGE(TAG, "FW overflow: %lu+%u > %lu",
                 (unsigned long)s_total_received, payload_len, (unsigned long)s_fw_total_size);
        bt_cleanup();
        s_gen = BLE_OTA_INVALID_GEN;
        ble_srv_ota_finish(gen, BLE_OTA_STATE_ERROR, BLE_OTA_ERR_INVALID_SIZE);
        return false;
    }

    esp_err_t write_err = ESP_OK;

    if (s_write_buf_len + payload_len > BLE_SRV_WRITE_BUF_SIZE) {
        if (s_write_buf_len > 0) {
            write_err = esp_ota_write(s_ota_handle, s_write_buf, s_write_buf_len);
            if (write_err != ESP_OK) {
                BLE_SRV_LOGE(TAG, "Flash write fail: %s", esp_err_to_name(write_err));
                bt_cleanup();
                s_gen = BLE_OTA_INVALID_GEN;
                ble_srv_ota_finish(gen, BLE_OTA_STATE_ERROR, BLE_OTA_ERR_FLASH_WRITE);
                return false;
            }
            s_fw_bytes_written += s_write_buf_len;
            s_write_buf_len = 0;
        }
    }

    memcpy(s_write_buf + s_write_buf_len, payload, payload_len);
    s_running_crc = crc32_update(s_running_crc, payload, payload_len);
    s_write_buf_len += payload_len;
    s_total_received += payload_len;
    s_packet_count++;

    bool flash_flushed = false;
    if (s_write_buf_len >= BLE_SRV_WRITE_BUF_SIZE) {
        write_err = esp_ota_write(s_ota_handle, s_write_buf, s_write_buf_len);
        if (write_err != ESP_OK) {
            BLE_SRV_LOGE(TAG, "Flash write fail: %s", esp_err_to_name(write_err));
            bt_cleanup();
            s_gen = BLE_OTA_INVALID_GEN;
            ble_srv_ota_finish(gen, BLE_OTA_STATE_ERROR, BLE_OTA_ERR_FLASH_WRITE);
            return false;
        }
        s_fw_bytes_written += s_write_buf_len;
        s_write_buf_len = 0;
        flash_flushed = true;
    }

    bool do_ack = false;
    if (flash_flushed || s_packet_count >= BLE_SRV_ACK_BATCH) {
        s_packet_count = 0;
        do_ack = true;
    }
    if (s_total_received >= s_fw_total_size) {
        do_ack = true;
        s_packet_count = 0;
    }

    if (do_ack) {
        uint32_t total_received = s_total_received;
        uint32_t bytes_written = s_fw_bytes_written;
        if (ble_srv_ota_gen_valid(gen)) {
            ble_srv_ota_report_progress(gen, total_received, bytes_written);
            ble_srv_ota_push_status(gen);
        }
    }

    return true;
}

bool ble_srv_ota_bt_process_fw_data(const uint8_t *data, uint16_t len)
{
    BT_LOCK();
    bool result = process_fw_data_locked(data, len);
    BT_UNLOCK();
    return result;
}
