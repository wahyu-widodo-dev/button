#ifndef BUTTON_H
#define BUTTON_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

   /**
    * @brief Jenis event tombol
    */
   typedef enum
   {
      BUTTON_EVENT_PRESSED = 0,  /*!< Tombol mulai ditekan (setelah debounce) */
      BUTTON_EVENT_RELEASED,     /*!< Tombol dilepas (setelah debounce) */
      BUTTON_EVENT_SINGLE_CLICK, /*!< Klik tunggal terdeteksi */
      BUTTON_EVENT_DOUBLE_CLICK, /*!< Klik ganda terdeteksi */
      BUTTON_EVENT_LONG_PRESS,   /*!< Tekan lama terdeteksi */
   } button_event_t;

   /**
    * @brief Callback event tombol, dipanggil dari konteks pemanggil button_tick().
    */
   typedef void (*button_callback_t)(button_event_t event, void *user_data);

   /**
    * @brief Konfigurasi Button
    */
   typedef struct
   {
      int gpio_num;     /*!< GPIO Pin untuk tombol */
      int active_level; /*!< Logika aktif tombol (0 untuk LOW, 1 untuk HIGH) */

      uint32_t debounce_ms;     /*!< Waktu debounce (default: 50ms jika 0) */
      uint32_t long_press_ms;   /*!< Durasi untuk tekan lama (default: 800ms jika 0) */
      uint32_t double_click_ms; /*!< Jendela klik ganda; 0 menonaktifkannya */

      button_callback_t callback; /*!< Fungsi callback untuk event tombol */
      void *user_data;            /*!< Data pengguna untuk dilewatkan ke callback */
   } button_config_t;

   /**
    * @brief Handle untuk instansi tombol
    */
   typedef struct button_t *button_handle_t;

   /**
    * @brief Inisialisasi tombol baru
    *
    * @param config Pointer ke konfigurasi tombol
    * @param out_handle Pointer untuk menyimpan handle tombol hasil inisialisasi
    * @return esp_err_t ESP_OK jika berhasil
    */
   esp_err_t button_init(const button_config_t *config, button_handle_t *out_handle);

   /**
    * @brief Proses/perbarui state mesin tombol.
    *
    * Fungsi ini harus dipanggil secara periodik dari task, bukan ISR.
    * Callback dijalankan secara sinkron selama fungsi ini berjalan.
    *
    * @param handle Handle tombol
    * @param elapsed_ms Selisih waktu sejak pemanggilan terakhir dalam milidetik
    */
   void button_tick(button_handle_t handle, uint32_t elapsed_ms);

   /**
    * @brief Menghapus instansi tombol dan membebaskan resource
    *
    * @param handle Handle tombol
    * @return esp_err_t ESP_OK jika berhasil
    */
   esp_err_t button_deinit(button_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // BUTTON_H
