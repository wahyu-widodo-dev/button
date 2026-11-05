# Komponen Button

Driver tombol GPIO berbasis polling untuk ESP-IDF 5.3.0 atau lebih baru.
Komponen menyediakan event tekan, lepas, klik tunggal, klik ganda, dan tekan
lama. Tidak menggunakan interrupt; aplikasi harus memanggil `button_tick()`
secara periodik dari task.

## Fitur

- Debounce untuk transisi tekan dan lepas.
- Event `PRESSED`, `RELEASED`, `SINGLE_CLICK`, `DOUBLE_CLICK`, dan `LONG_PRESS`.
- Level aktif HIGH atau LOW.
- Pull-down internal untuk tombol aktif-HIGH dan pull-up internal untuk
  tombol aktif-LOW.
- Callback dan data pengguna opsional.

## Koneksi

Hubungkan salah satu sisi tombol ke GPIO dan sisi lainnya ke GND untuk tombol
aktif-LOW, atau ke 3V3 untuk tombol aktif-HIGH. Driver mengaktifkan pull-up
atau pull-down internal yang sesuai. Pull internal relatif lemah; gunakan
resistor eksternal bila rangkaian atau panjang kabel memerlukannya. Pastikan
tegangan pada GPIO tidak melebihi batas SoC.

GPIO harus valid pada SoC yang dipakai. Pin input-only dapat digunakan karena
driver hanya mengkonfigurasikannya sebagai input.

## Konfigurasi

| Field | Default | Keterangan |
|---|---:|---|
| `gpio_num` | Tidak ada | GPIO tombol; wajib diisi |
| `active_level` | Tidak ada | `0` untuk aktif-LOW atau `1` untuk aktif-HIGH |
| `debounce_ms` | 50 ms | Waktu stabil untuk menerima perubahan level |
| `long_press_ms` | 800 ms | Durasi tekan setelah debounce untuk event tekan lama |
| `double_click_ms` | 0 (nonaktif) | Jendela menunggu klik kedua; `0` mengirim klik tunggal saat tombol dilepas |
| `callback` | `NULL` | Callback untuk event |
| `user_data` | `NULL` | Pointer yang diteruskan ke callback |

## Contoh penggunaan

Contoh ini menggunakan task FreeRTOS dengan periode polling 10 ms. Sesuaikan
periode dan nilai `elapsed_ms` dengan jadwal task aplikasi. GPIO pada contoh
hanya ilustrasi; periksa pin strapping dan batasan pin pada board yang dipakai.

```c
#include "button.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void on_button_event(button_event_t event, void *user_data)
{
    (void)user_data;
    /* Tangani event; callback berjalan di task pemanggil button_tick(). */
}

void app_main(void)
{
    const button_config_t config = {
        .gpio_num = GPIO_NUM_0,
        .active_level = 0,
        .debounce_ms = 50,
        .long_press_ms = 800,
        .double_click_ms = 300,
        .callback = on_button_event,
        .user_data = NULL,
    };

    button_handle_t button = NULL;
    ESP_ERROR_CHECK(button_init(&config, &button));

    while (true) {
        button_tick(button, 10);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

Inisialisasi GPIO dilakukan oleh `button_init()`. Jangan gunakan GPIO yang sama
secara bersamaan dengan driver lain. `button_tick()` harus dipanggil dari task,
bukan ISR; fungsi ini membaca GPIO dan dapat memanggil callback secara
sinkron. Event `DOUBLE_CLICK` dikirim saat tekanan kedua telah melewati
debounce, sehingga event ini dapat terjadi sebelum `RELEASED` kedua. Tekanan
kedua tersebut tidak menghasilkan event `LONG_PRESS`. Hindari operasi
blocking yang lama di callback.

Setelah `button_deinit()`, alokasi handle dibebaskan, tetapi konfigurasi GPIO
tidak dikembalikan otomatis ke keadaan sebelumnya. Hentikan task polling
sebelum melepas handle, lalu panggil:

```c
ESP_ERROR_CHECK(button_deinit(button));
button = NULL;
```

## Distribusi

Komponen ini didaftarkan sebagai komponen ESP-IDF melalui `CMakeLists.txt` dan
`idf_component.yml`, serta dilisensikan di bawah MIT.
