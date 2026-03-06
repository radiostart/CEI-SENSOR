/**
 * @file epd_driver.c
 * @brief SSD1680 2.13" BW(250x122) driver with REAL partial refresh
 *
 * IMPORTANT (BW 전용):
 * - 0x24 : NEW image RAM
 * - 0x26 : OLD image RAM (부분갱신 필수)
 *
 * 부분갱신이 흐려지는/안되는 가장 흔한 원인:
 * - 0x26(OLD)를 안 쓰거나, OLD/NEW를 같은 영역에 안 써서 첫 partial이 ghosting
 * - Partial IN(0x91)/OUT(0x92) 없이 트리거
 * - Partial window(0x90) 미설정
 * - Partial LUT(0x32) 미로드
 */

#include "epd_driver.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "epd_font.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "EPD_DRV";

// ============================================================
// SPI
// ============================================================
static spi_device_handle_t s_spi = NULL;

#define EPD_SPI_HOST SPI2_HOST
#define EPD_SPI_SPEED_HZ 4000000
#define EPD_TIMEOUT_MS 10000

// ============================================================
// Geometry
// ============================================================
#define EPD_LINE_BYTES (EPD_RAM_WIDTH / 8)            // 16
#define EPD_BUFFER_SIZE (EPD_LINE_BYTES * EPD_HEIGHT) // 4000

// ============================================================
// Framebuffers
// ============================================================
// SSD1680: bit=1 => WHITE, bit=0 => BLACK
static uint8_t s_fb_new[EPD_BUFFER_SIZE];
static uint8_t s_fb_old[EPD_BUFFER_SIZE];

// ============================================================
// Partial LUT (GxEPD2_213_BN 기반, SSD1680 BW partial)
// ============================================================
static const uint8_t s_lut_partial_bn[] = {
    0x0,  0x40, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0,  0x0, 0x0, 0x0,  0x80,
    0x80, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0,  0x0, 0x0, 0x40, 0x40,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0,  0x0, 0x0, 0x80, 0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0,  0x0, 0x0, 0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0A, 0x0, 0x0, 0x0,  0x0,
    0x0,  0x5,  0x1,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0,  0x1, 0x0, 0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0,  0x0, 0x0, 0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0,  0x0, 0x0, 0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0,  0x0, 0x0, 0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0,  0x0, 0x0, 0x0,  0x0,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0,  0x0, 0x0, 0x0,  0x0,
    0x0,  0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x0, 0x0,  0x0,
};

// ============================================================
// Low-level helpers
// ============================================================
static inline int epd_busy_level(void) {
#if EPD_BUSY_IS_HIGH
  return 1;
#else
  return 0;
#endif
}

static void epd_wait_busy(int timeout_ms) {
  int elapsed = 0;
  vTaskDelay(pdMS_TO_TICKS(5));

  while (gpio_get_level(EPD_PIN_BUSY) == epd_busy_level()) {
    vTaskDelay(pdMS_TO_TICKS(20));
    elapsed += 20;
    if (elapsed >= timeout_ms) {
      ESP_LOGW(TAG, "BUSY timeout: %dms (busy=%d)", elapsed,
               gpio_get_level(EPD_PIN_BUSY));
      break;
    }
  }
}

static void epd_gpio_init(void) {
  gpio_config_t out_conf = {
      .pin_bit_mask =
          (1ULL << EPD_PIN_RST) | (1ULL << EPD_PIN_DC) | (1ULL << EPD_PIN_CS),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&out_conf);

  gpio_config_t in_conf = {
      .pin_bit_mask = (1ULL << EPD_PIN_BUSY),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
#ifdef EPD_BUSY_PULLDOWN_ENABLE
      .pull_down_en = GPIO_PULLDOWN_ENABLE,
#else
      .pull_down_en = GPIO_PULLDOWN_DISABLE, // Default: Floating/External Pull
#endif
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&in_conf);

  gpio_set_level(EPD_PIN_CS, 1);
  gpio_set_level(EPD_PIN_DC, 1);
  gpio_set_level(EPD_PIN_RST, 1);
}

static void epd_spi_deinit(void) {
  if (s_spi) {
    spi_bus_remove_device(s_spi);
    s_spi = NULL;
    spi_bus_free(EPD_SPI_HOST);
  }
}

static esp_err_t epd_spi_init(void) {
  if (s_spi != NULL) return ESP_OK;  // 이미 초기화됨

  spi_bus_config_t bus_cfg = {
      .mosi_io_num = EPD_PIN_DIN,
      .miso_io_num = -1,
      .sclk_io_num = EPD_PIN_CLK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = EPD_BUFFER_SIZE + 16,
  };

  esp_err_t ret = spi_bus_initialize(EPD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
  if (ret != ESP_OK)
    return ret;

  spi_device_interface_config_t dev_cfg = {
      .clock_speed_hz = EPD_SPI_SPEED_HZ,
      .mode = 0,
      .spics_io_num = -1, // CS 수동
      .queue_size = 1,
  };

  return spi_bus_add_device(EPD_SPI_HOST, &dev_cfg, &s_spi);
}

static void epd_spi_send_byte(uint8_t data) {
  spi_transaction_t t = {
      .length = 8,
      .tx_buffer = &data,
  };
  spi_device_transmit(s_spi, &t);
}

static void epd_send_command(uint8_t cmd) {
  gpio_set_level(EPD_PIN_DC, 0);
  gpio_set_level(EPD_PIN_CS, 0);
  epd_spi_send_byte(cmd);
  gpio_set_level(EPD_PIN_CS, 1);
}

static void epd_send_data(uint8_t data) {
  gpio_set_level(EPD_PIN_DC, 1);
  gpio_set_level(EPD_PIN_CS, 0);
  epd_spi_send_byte(data);
  gpio_set_level(EPD_PIN_CS, 1);
}

// CS를 유지한 채로 chunk 전송(artifact 줄임)
static void epd_send_data_buf(const uint8_t *data, int len) {
  gpio_set_level(EPD_PIN_DC, 1);
  gpio_set_level(EPD_PIN_CS, 0);

  while (len > 0) {
    int chunk = (len > 4096) ? 4096 : len;
    spi_transaction_t t = {
        .length = chunk * 8,
        .tx_buffer = data,
    };
    spi_device_transmit(s_spi, &t);
    data += chunk;
    len -= chunk;
  }

  gpio_set_level(EPD_PIN_CS, 1);
}

static void epd_reset(void) {
  gpio_set_level(EPD_PIN_RST, 1);
  vTaskDelay(pdMS_TO_TICKS(50));
  gpio_set_level(EPD_PIN_RST, 0);
  vTaskDelay(pdMS_TO_TICKS(10));
  gpio_set_level(EPD_PIN_RST, 1);
  vTaskDelay(pdMS_TO_TICKS(50));
  epd_wait_busy(EPD_TIMEOUT_MS);
}

// ============================================================
// Mapping + framebuffer pixel set
// ============================================================
static inline void map_user_to_ram(int x, int y, int *rx, int *ry) {
  int ux = x;
  int uy = y;

  // flip은 "패널 기준"으로 먼저
  if (EPD_FLIP_X)
    ux = (EPD_PANEL_WIDTH - 1) - ux;
  if (EPD_FLIP_Y)
    uy = (EPD_HEIGHT - 1) - uy;

  // 그 다음 RAM 내 배치 오프셋
  int xx = ux + (int)EPD_X_OFFSET_PX;
  int yy = uy;

  if (xx < 0)
    xx = 0;
  if (xx >= EPD_RAM_WIDTH)
    xx = EPD_RAM_WIDTH - 1;
  if (yy < 0)
    yy = 0;
  if (yy >= EPD_HEIGHT)
    yy = EPD_HEIGHT - 1;

  *rx = xx;
  *ry = yy;
}

static inline void set_pixel_ram(uint8_t *buf, int rx, int ry, bool white) {
  if (rx < 0 || rx >= EPD_RAM_WIDTH || ry < 0 || ry >= EPD_HEIGHT)
    return;

  int byte_idx = ry * EPD_LINE_BYTES + (rx / 8);
  int bit_idx = 7 - (rx % 8);

  if (white)
    buf[byte_idx] |= (1 << bit_idx);
  else
    buf[byte_idx] &= ~(1 << bit_idx);
}

static inline void toggle_pixel_ram(uint8_t *buf, int rx, int ry) {
  if (rx < 0 || rx >= EPD_RAM_WIDTH || ry < 0 || ry >= EPD_HEIGHT)
    return;

  int byte_idx = ry * EPD_LINE_BYTES + (rx / 8);
  int bit_idx = 7 - (rx % 8);

  buf[byte_idx] ^= (1 << bit_idx);
}

// ============================================================
// Masking Helper (Non-destructive, Helper)
// ============================================================
static uint8_t get_mask_left(void) {
  // Left Dummy: Pixels 0 .. offset-1
  // Byte 0. MSB(bit7) is px0.
  // if offset=6, px0..5 -> bits 7..2 -> 11111100 -> 0xFC
  int offset = EPD_X_OFFSET_PX;
  if (offset <= 0)
    return 0x00;
  if (offset > 8)
    offset = 8;
  return (uint8_t)(0xFF00 >>
                   offset); // e.g. 6 -> 0xFF00 >> 6 = 0x03FC -> cast(0xFC)
}

static uint8_t get_mask_right(void) {
  // Right Dummy: Pixels (128-rdummy) .. 127
  // Total dummy = 128 - 122 = 6
  // rdummy = 6 - offset
  int total_dummy = EPD_RAM_WIDTH - EPD_PANEL_WIDTH; // 6
  int l_dummy = EPD_X_OFFSET_PX;
  if (l_dummy < 0)
    l_dummy = 0;
  int r_dummy = total_dummy - l_dummy;

  if (r_dummy <= 0)
    return 0x00;
  if (r_dummy > 8)
    r_dummy = 8;

  // Last Byte (Byte 15). Pixels 120..127.
  // px 127 is bit 0.
  // r_dummy=6 -> bits 5..0 -> 00111111 -> 0x3F
  // (1 << 6) - 1 = 63 = 0x3F
  return (uint8_t)((1 << r_dummy) - 1);
}

// Send a single line (16 bytes) with masking applied
static void epd_send_line_masked(const uint8_t *src_line) {
  uint8_t temp[EPD_LINE_BYTES];
  memcpy(temp, src_line, EPD_LINE_BYTES);

  // Apply Masks (OR with 1 for White)
  temp[0] |= get_mask_left();
  temp[EPD_LINE_BYTES - 1] |= get_mask_right();

  epd_send_data_buf(temp, EPD_LINE_BYTES);
}

// Helper to send entire framebuffer with on-the-fly masking
static void epd_send_buffer_masked(const uint8_t *fb) {
  for (int y = 0; y < EPD_HEIGHT; y++) {
    const uint8_t *line_ptr = &fb[y * EPD_LINE_BYTES];
    epd_send_line_masked(line_ptr);
  }
}

// ============================================================
// SSD1680 commands (window/cursor/LUT/update)
// ============================================================
static void epd_set_full_window_and_cursor(void) {
  // X window in bytes: 0..15
  epd_send_command(0x44);
  epd_send_data(0x00);
  epd_send_data(EPD_LINE_BYTES - 1);

  // Y window: 0..249
  epd_send_command(0x45);
  epd_send_data(0x00);
  epd_send_data(0x00);
  epd_send_data((EPD_HEIGHT - 1) & 0xFF);
  epd_send_data(((EPD_HEIGHT - 1) >> 8) & 0xFF);

  // cursor
  epd_send_command(0x4E);
  epd_send_data(0x00);
  epd_send_command(0x4F);
  epd_send_data(0x00);
  epd_send_data(0x00);
}

static void epd_load_partial_lut(void) {
  epd_send_command(0x32);
  epd_send_data_buf(s_lut_partial_bn, sizeof(s_lut_partial_bn));
}

static void epd_update_full(void) {
  epd_send_command(0x22);
  epd_send_data(0xF7); // 0xF7: Auto sequence (Standard)
  epd_send_command(0x20);
  epd_wait_busy(EPD_TIMEOUT_MS);
}

static void epd_update_partial_trigger(void) {
  // border for partial
  epd_send_command(0x3C);
  epd_send_data(0x80);

  epd_send_command(0x22);
  epd_send_data(0xFF); // partial update mode
  epd_send_command(0x20);
  epd_wait_busy(EPD_TIMEOUT_MS);
}

// SSD1680 partial window command (0x90)
// X is Byte Address (x/8), Y is Little Endian, X needs 8-pixel alignment
static void epd_set_partial_window_px(int x, int y, int w, int h) {
  int x0 = x;
  int y0 = y;
  int x1 = x + w - 1;
  int y1 = y + h - 1;

  // clamp
  if (x0 < 0)
    x0 = 0;
  if (y0 < 0)
    y0 = 0;
  if (x1 >= EPD_RAM_WIDTH)
    x1 = EPD_RAM_WIDTH - 1;
  if (y1 >= EPD_HEIGHT)
    y1 = EPD_HEIGHT - 1;

  // align X to byte
  x0 &= ~7;
  x1 |= 7;

  epd_send_command(0x90);          // PARTIAL_WINDOW
  epd_send_data(x0 / 8);           // X Start Byte
  epd_send_data(x1 / 8);           // X End Byte
  epd_send_data(y0 & 0xFF);        // Y Start Low
  epd_send_data((y0 >> 8) & 0xFF); // Y Start High
  epd_send_data(y1 & 0xFF);        // Y End Low
  epd_send_data((y1 >> 8) & 0xFF); // Y End High
  epd_send_data(0x01);             // Gatescan
}

// RAM X/Y window/cursor for writing (0x44/0x45/0x4E/0x4F)
static void epd_set_ram_window_and_cursor_for_area(int ram_x0, int ram_y0,
                                                   int ram_x1, int ram_y1) {
  int col0 = ram_x0 / 8;
  int col1 = ram_x1 / 8;

  epd_send_command(0x44);
  epd_send_data(col0);
  epd_send_data(col1);

  epd_send_command(0x45);
  epd_send_data(ram_y0 & 0xFF);
  epd_send_data((ram_y0 >> 8) & 0xFF);
  epd_send_data(ram_y1 & 0xFF);
  epd_send_data((ram_y1 >> 8) & 0xFF);

  epd_send_command(0x4E);
  epd_send_data(col0);

  epd_send_command(0x4F);
  epd_send_data(ram_y0 & 0xFF);
  epd_send_data((ram_y0 >> 8) & 0xFF);
}

// ============================================================
// SSD1680 init sequence (BW)
// ============================================================
static void epd_init_sequence(void) {
  epd_reset();

  // SW Reset
  epd_send_command(0x12);
  vTaskDelay(pdMS_TO_TICKS(50));
  epd_wait_busy(EPD_TIMEOUT_MS);

  // Booster Soft Start (Stabilize voltages)
  epd_send_command(0x0C);
  epd_send_data(0xD7);
  epd_send_data(0xD6);
  epd_send_data(0x9D);

  // Driver Output Control
  epd_send_command(0x01);
  epd_send_data((EPD_HEIGHT - 1) & 0xFF);
  epd_send_data(((EPD_HEIGHT - 1) >> 8) & 0xFF);
  epd_send_data(0x00);

  // Source Driving Voltage Control (3.3V 시스템 최적화)
  epd_send_command(0x04);
  epd_send_data(0x41); // VSH1
  epd_send_data(0xA8); // VSH2
  epd_send_data(0x32); // VSL

  // Data Entry Mode (X+, Y+)
  epd_send_command(0x11);
  epd_send_data(0x03);

  // RAM X/Y window full
  epd_set_full_window_and_cursor();

  // Border Waveform Control
  // 0x01: Follow LUT (VCOM)
  // 0x05: GS Transition (default)
  // 0x03: Fix Level (VSS) - 노이즈 감소에 도움
  epd_send_command(0x3C);
  epd_send_data(0x01);

  // Display Update Control 1 - Source output mode
  // Bit 7: 0=Normal, 1=Bypass (Source output level to GND)
  // Bit 4: 0=Source S8-S167 (for 122 columns), Source S0-S7 output at GND
  epd_send_command(0x21);
  epd_send_data(0x00);
  epd_send_data(0x80); // Bypass dummy source outputs

  // Temperature Sensor internal
  epd_send_command(0x18);
  epd_send_data(0x80);

  epd_wait_busy(EPD_TIMEOUT_MS);

  // Load partial LUT once at init (또는 partial 때마다 로드해도 됨)
  epd_load_partial_lut();
}

// ============================================================
// Public API
// ============================================================
int epd_init(void) {
  ESP_LOGI(TAG,
           "BW SSD1680 init: panel=%dx%d, ram=%dx%d, xoff=%d flipX=%d flipY=%d "
           "busyHigh=%d",
           EPD_PANEL_WIDTH, EPD_HEIGHT, EPD_RAM_WIDTH, EPD_HEIGHT,
           (int)EPD_X_OFFSET_PX, (int)EPD_FLIP_X, (int)EPD_FLIP_Y,
           (int)EPD_BUSY_IS_HIGH);

  epd_gpio_init();

  esp_err_t ret = epd_spi_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SPI init failed: %s", esp_err_to_name(ret));
    return -1;
  }

  // buffers = white
  memset(s_fb_new, 0xFF, sizeof(s_fb_new));
  memset(s_fb_old, 0xFF, sizeof(s_fb_old));

  // No destructive masking here. Masking happens during send.

  epd_init_sequence();

  // 실제 패널도 한번 깨끗이 흰색으로 맞추고 OLD/NEW 동기화
  epd_set_full_window_and_cursor();
  epd_send_command(0x24);           // NEW
  epd_send_buffer_masked(s_fb_new); // Apply mask on fly

  epd_set_full_window_and_cursor();
  epd_send_command(0x26);           // OLD
  epd_send_buffer_masked(s_fb_old); // Apply mask on fly

  epd_update_full();
  return 0;
}

void epd_sleep(void) {
  epd_send_command(0x10);
  epd_send_data(0x01);
  // SPI 버스 해제 (슬립 중 전력 절약)
  epd_spi_deinit();
}

// Wakeup: SPI 재초기화 + Init + Buffer Sync for safety
void epd_wakeup(void) {
  if (epd_spi_init() != ESP_OK) {
    ESP_LOGW(TAG, "SPI re-init failed in wakeup, retrying...");
    epd_spi_deinit();
    if (epd_spi_init() != ESP_OK) {
      ESP_LOGE(TAG, "SPI re-init failed after retry");
      return;
    }
  }
  epd_init_sequence();

  // Sync Controller RAM with current s_fb_new to prevent state mismatch
  // OPTION: Uncomment if screen corruption occurs after wake
  // #define EPD_WAKE_FULL_REFRESH_ENABLE

#ifdef EPD_WAKE_FULL_REFRESH_ENABLE
  epd_refresh();
#else
  // Standard: Just sync RAM so next partial is clean
  epd_set_full_window_and_cursor();
  epd_send_command(0x24);
  epd_send_buffer_masked(s_fb_new);

  epd_set_full_window_and_cursor();
  epd_send_command(0x26);
  epd_send_buffer_masked(s_fb_new);
#endif
}

void epd_clear(epd_color_t color) {
  if (color == EPD_COLOR_WHITE)
    memset(s_fb_new, 0xFF, sizeof(s_fb_new));
  else
    memset(s_fb_new, 0x00, sizeof(s_fb_new));
}

void epd_draw_pixel(int x, int y, epd_color_t color) {
  if (x < 0 || x >= EPD_PANEL_WIDTH || y < 0 || y >= EPD_HEIGHT)
    return;

  int rx, ry;
  map_user_to_ram(x, y, &rx, &ry);

  if (color == EPD_COLOR_INVERT) {
    toggle_pixel_ram(s_fb_new, rx, ry);
  } else {
    bool white = (color == EPD_COLOR_WHITE);
    set_pixel_ram(s_fb_new, rx, ry, white);
  }
}

void epd_refresh(void) {
  // 1. Sync OLD (0x26) to be identical to NEW (Masked)
  // This is CRITICAL. OLD must equal NEW in dummy areas to avoid ghosting.
  epd_set_full_window_and_cursor();
  epd_send_command(0x26);
  epd_send_buffer_masked(s_fb_new);

  // 2. Load NEW (0x24) (Masked)
  epd_set_full_window_and_cursor();
  epd_send_command(0x24);
  epd_send_buffer_masked(s_fb_new);

  // 3. Perform Update
  epd_update_full();

  // 4. Update SW Buffer
  memcpy(s_fb_old, s_fb_new, sizeof(s_fb_old));

  // Note: HW OLD is already s_fb_new from step 1, but we sync it again for next
  // partial if needed. Actually step 1 is sufficient as "Post-Update State".
}

// ============================================================
// Partial write (RAM only, no trigger)
// ============================================================
void epd_write_area_to_ram(int x, int y, int w, int h) {
  if (w <= 0 || h <= 0)
    return;

  // 1) user rect -> RAM rect
  int x0 = x, y0 = y, x1 = x + w - 1, y1 = y + h - 1;

  if (x0 < 0)
    x0 = 0;
  if (y0 < 0)
    y0 = 0;
  if (x1 >= EPD_PANEL_WIDTH)
    x1 = EPD_PANEL_WIDTH - 1;
  if (y1 >= EPD_HEIGHT)
    y1 = EPD_HEIGHT - 1;
  if (x0 > x1 || y0 > y1)
    return;

  int rx0, ry0, rx1, ry1;
  map_user_to_ram(x0, y0, &rx0, &ry0);
  map_user_to_ram(x1, y1, &rx1, &ry1);

  int ram_x_min = (rx0 < rx1) ? rx0 : rx1;
  int ram_x_max = (rx0 > rx1) ? rx0 : rx1;
  int ram_y_min = (ry0 < ry1) ? ry0 : ry1;
  int ram_y_max = (ry0 > ry1) ? ry0 : ry1;

  // align X
  ram_x_min &= ~7;
  ram_x_max |= 7;
  if (ram_x_min < 0)
    ram_x_min = 0;
  if (ram_x_max >= EPD_RAM_WIDTH)
    ram_x_max = EPD_RAM_WIDTH - 1;

  // 2) Partial IN
  epd_send_command(0x91);
  epd_set_partial_window_px(ram_x_min, ram_y_min, (ram_x_max - ram_x_min + 1),
                            (ram_y_max - ram_y_min + 1));
  epd_set_ram_window_and_cursor_for_area(ram_x_min, ram_y_min, ram_x_max,
                                         ram_y_max);

  int col0 = ram_x_min / 8;
  int col1 = ram_x_max / 8;
  int bytes_per_line = col1 - col0 + 1;

  // Decide if we are hitting edges for masking
  // If (col0 == 0) we might need left mask
  // If (col1 == 15) we might need right mask
  uint8_t m_left = get_mask_left();
  uint8_t m_right = get_mask_right();

  // Temp line buffer (16 bytes max, but we only need bytes_per_line)
  // We can just iterate per line.
  uint8_t temp[16];

  // 3) OLD (0x26)
  epd_send_command(0x26);
  for (int yy = ram_y_min; yy <= ram_y_max; yy++) {
    int idx = yy * EPD_LINE_BYTES + col0;
    memcpy(temp, &s_fb_old[idx], bytes_per_line);

    // Apply Mask if at edge
    if (col0 == 0)
      temp[0] |= m_left;
    if (col1 == (EPD_RAM_WIDTH / 8 - 1))
      temp[bytes_per_line - 1] |= m_right;

    epd_send_data_buf(temp, bytes_per_line);
  }

  // 4) NEW (0x24)
  epd_set_ram_window_and_cursor_for_area(ram_x_min, ram_y_min, ram_x_max,
                                         ram_y_max);
  epd_send_command(0x24);
  for (int yy = ram_y_min; yy <= ram_y_max; yy++) {
    int idx = yy * EPD_LINE_BYTES + col0;
    memcpy(temp, &s_fb_new[idx], bytes_per_line);

    // Apply Mask if at edge
    if (col0 == 0)
      temp[0] |= m_left;
    if (col1 == (EPD_RAM_WIDTH / 8 - 1))
      temp[bytes_per_line - 1] |= m_right;

    epd_send_data_buf(temp, bytes_per_line);
  }
}

// ============================================================
// Partial trigger
// ============================================================
void epd_update_display_partial(void) {
  // partial LUT
  epd_load_partial_lut();

  // 1. Real Update (Old -> New)
  epd_update_partial_trigger();

  // 2. Partial Out (Safe State)
  epd_send_command(0x92);

  // 3. Sync SW Buffer (Old = New)
  memcpy(s_fb_old, s_fb_new, sizeof(s_fb_old));

  // No Redundant HW Sync (Already handled by epd_write_area_to_ram for next
  // frame)
}

void epd_refresh_partial_area(int x, int y, int w, int h) {
  epd_write_area_to_ram(x, y, w, h);
  epd_update_display_partial();
}

// ============================================================
// drawing primitives
// ============================================================
void epd_fill_rect(int x, int y, int w, int h, epd_color_t c) {
  for (int yy = y; yy < y + h; yy++)
    for (int xx = x; xx < x + w; xx++)
      epd_draw_pixel(xx, yy, c);
}

void epd_draw_rect(int x, int y, int w, int h, epd_color_t c) {
  for (int xx = x; xx < x + w; xx++) {
    epd_draw_pixel(xx, y, c);
    epd_draw_pixel(xx, y + h - 1, c);
  }
  for (int yy = y; yy < y + h; yy++) {
    epd_draw_pixel(x, yy, c);
    epd_draw_pixel(x + w - 1, yy, c);
  }
}

void epd_draw_hline(int x, int y, int length, epd_color_t c) {
  for (int i = 0; i < length; i++)
    epd_draw_pixel(x + i, y, c);
}

void epd_draw_vline(int x, int y, int length, epd_color_t c) {
  for (int i = 0; i < length; i++)
    epd_draw_pixel(x, y + i, c);
}

// ============================================================
// text
// ============================================================
void epd_draw_text(int x, int y, const char *text, epd_font_size_t size,
                   epd_color_t color) {
  if (!text)
    return;

  int char_w = EPD_FONT_SMALL_WIDTH;
  if (size == EPD_FONT_LARGE)
    char_w = EPD_FONT_SMALL_WIDTH * 2;

  int cx = x;
  while (*text) {
    char c = *text;
    if (c < 32 || c > 127)
      c = '?';

    int font_idx = (c - 32) * 6;

    for (int col = 0; col < 6; col++) {
      uint8_t col_data = font_6x8[font_idx + col];
      for (int row = 0; row < 8; row++) {
        if (col_data & (1 << row)) {
          if (size == EPD_FONT_LARGE) {
            epd_draw_pixel(cx + col * 2, y + row * 2, color);
            epd_draw_pixel(cx + col * 2 + 1, y + row * 2, color);
            epd_draw_pixel(cx + col * 2, y + row * 2 + 1, color);
            epd_draw_pixel(cx + col * 2 + 1, y + row * 2 + 1, color);
          } else {
            epd_draw_pixel(cx + col, y + row, color);
          }
        }
      }
    }

    cx += char_w;
    text++;
  }
}

int epd_get_text_width(const char *text, epd_font_size_t size) {
  if (!text)
    return 0;
  int len = (int)strlen(text);

  int char_w = EPD_FONT_SMALL_WIDTH;
  if (size == EPD_FONT_LARGE)
    char_w = EPD_FONT_SMALL_WIDTH * 2;
  return len * char_w;
}

void epd_draw_text_centered(int center_x, int y, const char *text,
                            epd_font_size_t size, epd_color_t color) {
  int tw = epd_get_text_width(text, size);
  epd_draw_text(center_x - tw / 2, y, text, size, color);
}