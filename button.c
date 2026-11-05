#include "button.h"
#include "driver/gpio.h"
#include <inttypes.h>
#include <stdlib.h>
#include "esp_log.h"
static const char *TAG = "button_driver";

static const char *button_event_name(button_event_t event)
{
   switch (event)
   {
   case BUTTON_EVENT_PRESSED:
      return "PRESSED";
   case BUTTON_EVENT_RELEASED:
      return "RELEASED";
   case BUTTON_EVENT_SINGLE_CLICK:
      return "SINGLE_CLICK";
   case BUTTON_EVENT_DOUBLE_CLICK:
      return "DOUBLE_CLICK";
   case BUTTON_EVENT_LONG_PRESS:
      return "LONG_PRESS";
   default:
      return "UNKNOWN";
   }
}

typedef enum
{
   BTN_STATE_IDLE = 0,
   BTN_STATE_DEBOUNCE_PRESS,
   BTN_STATE_PRESS_HOLD,
   BTN_STATE_LONG_PRESS_HOLD,
   BTN_STATE_DEBOUNCE_RELEASE,
   BTN_STATE_WAIT_DOUBLE,
} btn_internal_state_t;

struct button_t
{
   int gpio_num;
   int active_level;
   uint32_t debounce_ms;
   uint32_t long_press_ms;
   uint32_t double_click_ms;

   button_callback_t callback;
   void *user_data;

   btn_internal_state_t state;
   uint32_t state_timer_ms;
   uint8_t click_count;
   bool is_long_pressed; // Bendera internal untuk membedakan rilis pasca long press/double click
};

esp_err_t button_init(const button_config_t *config, button_handle_t *out_handle)
{
   if (out_handle == NULL)
   {
      ESP_LOGE(TAG, "Init: parameter konfigurasi atau output handle tidak valid");
      return ESP_ERR_INVALID_ARG;
   }

   *out_handle = NULL;
   if (config == NULL ||
       !GPIO_IS_VALID_GPIO(config->gpio_num) ||
       (config->active_level != 0 && config->active_level != 1))
   {
      ESP_LOGE(TAG, "Init: nomor GPIO atau active_level tidak valid");
      return ESP_ERR_INVALID_ARG;
   }

   button_handle_t dev = calloc(1, sizeof(struct button_t));
   if (dev == NULL)
   {
      ESP_LOGE(TAG, "Init: gagal mengalokasikan handle tombol");
      return ESP_ERR_NO_MEM;
   }

   dev->gpio_num = config->gpio_num;
   dev->active_level = config->active_level;
   dev->debounce_ms = (config->debounce_ms == 0) ? 50 : config->debounce_ms;
   dev->long_press_ms = (config->long_press_ms == 0) ? 800 : config->long_press_ms;
   dev->double_click_ms = config->double_click_ms;
   dev->callback = config->callback;
   dev->user_data = config->user_data;

   ESP_LOGI(TAG, "Konfigurasi: GPIO=%d, aktif=%s, mode=polling",
            dev->gpio_num, dev->active_level == 1 ? "HIGH" : "LOW");
   ESP_LOGI(TAG, "Konfigurasi: debounce=%" PRIu32 " ms, tekan lama=%" PRIu32 " ms, klik ganda=%s",
            dev->debounce_ms, dev->long_press_ms,
            dev->double_click_ms == 0 ? "nonaktif" : "aktif");
   ESP_LOGD(TAG, "Konfigurasi timing: rentang klik ganda=%" PRIu32 " ms, callback=%s",
            dev->double_click_ms, dev->callback != NULL ? "terpasang" : "tidak ada");
   if (config->debounce_ms == 0 || config->long_press_ms == 0)
   {
      ESP_LOGW(TAG, "Konfigurasi: gunakan default debounce=50 ms, tekan lama=800 ms");
   }

   dev->state = BTN_STATE_IDLE;
   dev->state_timer_ms = 0;
   dev->click_count = 0;
   dev->is_long_pressed = false;

   gpio_config_t io_conf = {
       .intr_type = GPIO_INTR_DISABLE, // Menggunakan polling waktu, tanpa ISR
       .mode = GPIO_MODE_INPUT,
       .pin_bit_mask = (1ULL << dev->gpio_num),
       .pull_down_en = (dev->active_level == 1) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
       .pull_up_en = (dev->active_level == 0) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
   };

   esp_err_t err = gpio_config(&io_conf);
   if (err != ESP_OK)
   {
      ESP_LOGE(TAG, "Setup: gagal konfigurasi GPIO %d (%s)",
               dev->gpio_num, esp_err_to_name(err));
      free(dev);
      return err;
   }

   *out_handle = dev;
   ESP_LOGD(TAG, "GPIO: input, interrupt=nonaktif, pull-up=%s, pull-down=%s",
            dev->active_level == 0 ? "aktif" : "nonaktif",
            dev->active_level == 1 ? "aktif" : "nonaktif");
   ESP_LOGI(TAG, "Setup: tombol aktif pada GPIO %d", dev->gpio_num);
   return ESP_OK;
}

void button_tick(button_handle_t handle, uint32_t elapsed_ms)
{
   if (handle == NULL)
      return;

   // Baca tingkat logika fisik tombol saat ini
   int pin_level = gpio_get_level(handle->gpio_num);
   bool is_pressed = (pin_level == (int)handle->active_level);

   handle->state_timer_ms += elapsed_ms;

   switch (handle->state)
   {
   case BTN_STATE_IDLE:
      if (is_pressed)
      {
         handle->state = BTN_STATE_DEBOUNCE_PRESS;
         handle->state_timer_ms = 0;
      }
      break;

   case BTN_STATE_DEBOUNCE_PRESS:
      if (!is_pressed)
      {
         // Bouncing/noise, kembalikan ke state sebelumnya
         if (handle->click_count > 0)
         {
            handle->state = BTN_STATE_WAIT_DOUBLE;
         }
         else
         {
            handle->state = BTN_STATE_IDLE;
         }
         handle->state_timer_ms = 0;
      }
      else if (handle->state_timer_ms >= handle->debounce_ms)
      {
         // Tekanan valid
         handle->state = BTN_STATE_PRESS_HOLD;
         handle->state_timer_ms = 0;

         if (handle->callback)
         {
            ESP_LOGV(TAG, "Event: %s", button_event_name(BUTTON_EVENT_PRESSED));
            handle->callback(BUTTON_EVENT_PRESSED, handle->user_data);
         }

         // Periksa jika ini adalah klik kedua untuk Double Click
         if (handle->click_count == 1)
         {
            if (handle->callback)
            {
               ESP_LOGV(TAG, "Event: %s", button_event_name(BUTTON_EVENT_DOUBLE_CLICK));
               handle->callback(BUTTON_EVENT_DOUBLE_CLICK, handle->user_data);
            }
            handle->click_count = 0;
            handle->is_long_pressed = true; // Set bendera untuk abaikan Single Click pasca rilis
         }
         else
         {
            handle->is_long_pressed = false;
         }
      }
      break;

   case BTN_STATE_PRESS_HOLD:
      if (!is_pressed)
      {
         handle->state = BTN_STATE_DEBOUNCE_RELEASE;
         handle->state_timer_ms = 0;
      }
      else if (!handle->is_long_pressed && handle->state_timer_ms >= handle->long_press_ms)
      {
         // Tekan lama valid
         handle->state = BTN_STATE_LONG_PRESS_HOLD;
         handle->state_timer_ms = 0;
         handle->is_long_pressed = true;
         if (handle->callback)
         {
            ESP_LOGV(TAG, "Event: %s", button_event_name(BUTTON_EVENT_LONG_PRESS));
            handle->callback(BUTTON_EVENT_LONG_PRESS, handle->user_data);
         }
      }
      break;

   case BTN_STATE_LONG_PRESS_HOLD:
      if (!is_pressed)
      {
         handle->state = BTN_STATE_DEBOUNCE_RELEASE;
         handle->state_timer_ms = 0;
      }
      break;

   case BTN_STATE_DEBOUNCE_RELEASE:
      if (is_pressed)
      {
         // Noise rilis
         handle->state = BTN_STATE_PRESS_HOLD;
         handle->state_timer_ms = 0;
      }
      else if (handle->state_timer_ms >= handle->debounce_ms)
      {
         // Rilis valid
         if (handle->callback)
         {
            ESP_LOGV(TAG, "Event: %s", button_event_name(BUTTON_EVENT_RELEASED));
            handle->callback(BUTTON_EVENT_RELEASED, handle->user_data);
         }

         if (handle->is_long_pressed)
         {
            // Pasca Long Press atau Double Click, langsung reset ke IDLE
            handle->state = BTN_STATE_IDLE;
            handle->click_count = 0;
         }
         else if (handle->double_click_ms == 0)
         {
            // Klik tunggal instan tanpa menunggu double click
            if (handle->callback)
            {
               ESP_LOGV(TAG, "Event: %s", button_event_name(BUTTON_EVENT_SINGLE_CLICK));
               handle->callback(BUTTON_EVENT_SINGLE_CLICK, handle->user_data);
            }
            handle->state = BTN_STATE_IDLE;
            handle->click_count = 0;
         }
         else
         {
            // Mulai timer menunggu klik kedua
            handle->state = BTN_STATE_WAIT_DOUBLE;
            handle->click_count = 1;
         }
         handle->state_timer_ms = 0;
      }
      break;

   case BTN_STATE_WAIT_DOUBLE:
      if (is_pressed)
      {
         // Klik kedua terdeteksi sebelum timeout
         handle->state = BTN_STATE_DEBOUNCE_PRESS;
         handle->state_timer_ms = 0;
      }
      else if (handle->state_timer_ms >= handle->double_click_ms)
      {
         // Batas waktu klik ganda tercapai, simpulkan sebagai Klik Tunggal
         if (handle->callback)
         {
            ESP_LOGV(TAG, "Event: %s", button_event_name(BUTTON_EVENT_SINGLE_CLICK));
            handle->callback(BUTTON_EVENT_SINGLE_CLICK, handle->user_data);
         }
         handle->state = BTN_STATE_IDLE;
         handle->click_count = 0;
         handle->state_timer_ms = 0;
      }
      break;

   default:
      handle->state = BTN_STATE_IDLE;
      break;
   }
}

esp_err_t button_deinit(button_handle_t handle)
{
   if (handle == NULL)
   {
      ESP_LOGE(TAG, "Deinit: handle tombol tidak valid");
      return ESP_ERR_INVALID_ARG;
   }

   free(handle);
   ESP_LOGI(TAG, "Deinit: driver tombol berhasil dilepas");
   return ESP_OK;
}
