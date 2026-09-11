#include "PhotoBadge.h"
#include "PhotoPage.h"
#include "RoundPaint.h"
// The Arduino SDK's lwIP binary links its IPv6 input hook from Networking.
#include <Network.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_event.h>
#include <esp_random.h>
#include <esp_http_server.h>
#include <esp_heap_caps.h>
#include <esp_rom_crc.h>
#include <atomic>

namespace Esp32 {
  class PhotoPortal {
  public:
    static constexpr size_t FrameBytes = 466 * 466 * 2;
    enum class Transfer { Idle, Receiving, Queued, Writing, Done, Failed };

    explicit PhotoPortal(Pinetime::Controllers::FS& fs) : fs(fs) {
      fs.FileDelete("/photo-upload.tmp");
      lfs_info info {};
      if (fs.Stat("/photo.rgb", &info) == 0 && info.size == FrameBytes) {
        auto* buffer = static_cast<uint8_t*>(heap_caps_malloc(FrameBytes, MALLOC_CAP_SPIRAM));
        lfs_file_t file {};
        if (buffer && fs.FileOpen(&file, "/photo.rgb", LFS_O_RDONLY) == 0) {
          const int count = fs.FileRead(&file, buffer, FrameBytes);
          fs.FileClose(&file);
          if (count == FrameBytes)
            image = buffer;
          else
            free(buffer);
        } else
          free(buffer);
      }
      descriptor.header.cf = LV_IMG_CF_TRUE_COLOR;
      descriptor.header.w = 466;
      descriptor.header.h = 466;
      descriptor.data_size = FrameBytes;
    }

    ~PhotoPortal() {
      Stop();
      lv_img_cache_invalidate_src(&descriptor);
      free(image);
    }

    bool Start() {
      if (active)
        return true;
      snprintf(password, sizeof(password), "%08lx", static_cast<unsigned long>(esp_random()));
      if (esp_netif_init() != ESP_OK)
        return false;
      const auto eventResult = esp_event_loop_create_default();
      eventLoopOwned = eventResult == ESP_OK;
      if (eventResult != ESP_OK && eventResult != ESP_ERR_INVALID_STATE)
        return false;
      netif = esp_netif_create_default_wifi_ap();
      wifi_init_config_t wifiInit = WIFI_INIT_CONFIG_DEFAULT();
      radioInitialized = esp_wifi_init(&wifiInit) == ESP_OK;
      wifi_config_t wifi {};
      strcpy(reinterpret_cast<char*>(wifi.ap.ssid), "InfiniTime-Badge");
      strcpy(reinterpret_cast<char*>(wifi.ap.password), password);
      wifi.ap.channel = 1;
      wifi.ap.max_connection = 1;
      wifi.ap.authmode = WIFI_AUTH_WPA2_PSK;
      if (!radioInitialized || esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK || esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK ||
          esp_wifi_set_config(WIFI_IF_AP, &wifi) != ESP_OK || esp_wifi_start() != ESP_OK) {
        Stop();
        return false;
      }
      httpd_config_t config = HTTPD_DEFAULT_CONFIG();
      config.stack_size = 6144;
      config.recv_wait_timeout = 3;
      config.send_wait_timeout = 3;
      if (httpd_start(&server, &config) != ESP_OK) {
        Stop();
        return false;
      }
      active = true;
      httpd_uri_t page {};
      page.uri = "/";
      page.method = HTTP_GET;
      page.handler = Home;
      page.user_ctx = this;
      httpd_uri_t frame {};
      frame.uri = "/frame";
      frame.method = HTTP_POST;
      frame.handler = Upload;
      frame.user_ctx = this;
      if (httpd_register_uri_handler(server, &page) != ESP_OK || httpd_register_uri_handler(server, &frame) != ESP_OK) {
        Stop();
        return false;
      }
      return true;
    }

    void Stop() {
      active = false;
      if (server) {
        httpd_stop(server);
        server = nullptr;
      }
      if (open) {
        fs.FileClose(&file);
        open = false;
      }
      free(incoming);
      incoming = nullptr;
      fs.FileDelete("/photo-upload.tmp");
      state = Transfer::Idle;
      if (radioInitialized) {
        esp_wifi_stop();
        esp_wifi_deinit();
        radioInitialized = false;
      }
      if (netif) {
        esp_netif_destroy_default_wifi(netif);
        netif = nullptr;
      }
      if (eventLoopOwned) {
        esp_event_loop_delete_default();
        eventLoopOwned = false;
      }
    }

    void Tick() {
      if (state == Transfer::Queued) {
        offset = 0;
        open = fs.FileOpen(&file, "/photo-upload.tmp", LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) == 0;
        state = open ? Transfer::Writing : Transfer::Failed;
      }
      if (state != Transfer::Writing)
        return;
      const size_t count = std::min<size_t>(4096, FrameBytes - offset);
      if (fs.FileWrite(&file, incoming + offset, count) != count) {
        fs.FileClose(&file);
        open = false;
        state = Transfer::Failed;
        return;
      }
      offset += count;
      if (offset == FrameBytes) {
        const bool closed = fs.FileClose(&file) == 0;
        open = false;
        if (!closed || fs.Rename("/photo-upload.tmp", "/photo.rgb") != 0) {
          state = Transfer::Failed;
          return;
        }
        lv_img_cache_invalidate_src(&descriptor);
        free(image);
        image = incoming;
        incoming = nullptr;
        ++version;
        state = Transfer::Done;
      }
    }

    const lv_img_dsc_t* Image() {
      descriptor.data = image;
      return image ? &descriptor : nullptr;
    }

    bool Active() const {
      return active;
    }

    const char* Password() const {
      return password;
    }

    unsigned Version() const {
      return version;
    }

    Transfer State() const {
      return state;
    }

  private:
    static esp_err_t Home(httpd_req_t* request) {
      httpd_resp_set_type(request, "text/html; charset=utf-8");
      httpd_resp_set_hdr(request, "Cache-Control", "no-store");
      return httpd_resp_send(request, PhotoPage, HTTPD_RESP_USE_STRLEN);
    }

    static esp_err_t Upload(httpd_req_t* request) {
      auto& self = *static_cast<PhotoPortal*>(request->user_ctx);
      auto fail = [&](const char* message) {
        self.state = Transfer::Failed;
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, message);
      };
      if (request->content_len != FrameBytes)
        return fail("Expected a 466 x 466 RGB565 frame");
      char crcText[10] {};
      char* end = nullptr;
      if (httpd_req_get_hdr_value_str(request, "X-CRC32", crcText, sizeof(crcText)) != ESP_OK || strlen(crcText) != 8)
        return fail("Missing checksum");
      const uint32_t expected = strtoul(crcText, &end, 16);
      if (*end)
        return fail("Invalid checksum");
      free(self.incoming);
      self.incoming = static_cast<uint8_t*>(heap_caps_malloc(FrameBytes, MALLOC_CAP_SPIRAM));
      if (!self.incoming)
        return fail("Insufficient image memory");
      self.state = Transfer::Receiving;
      size_t total = 0;
      while (self.active && total < FrameBytes) {
        const int count =
          httpd_req_recv(request, reinterpret_cast<char*>(self.incoming + total), std::min<size_t>(4096, FrameBytes - total));
        if (count <= 0)
          return fail("Transfer interrupted; old image retained");
        total += count;
      }
      if (!self.active)
        return ESP_FAIL;
      if (esp_rom_crc32_le(0, self.incoming, FrameBytes) != expected)
        return fail("Checksum mismatch; old image retained");
      self.state = Transfer::Queued;
      while (self.active && (self.state == Transfer::Queued || self.state == Transfer::Writing))
        vTaskDelay(pdMS_TO_TICKS(10));
      if (self.state != Transfer::Done)
        return fail("Could not save image; old image retained");
      return httpd_resp_sendstr(request, "Saved");
    }

    Pinetime::Controllers::FS& fs;
    httpd_handle_t server = nullptr;
    esp_netif_t* netif = nullptr;
    bool radioInitialized = false, eventLoopOwned = false;
    std::atomic<bool> active {false};
    std::atomic<Transfer> state {Transfer::Idle};
    uint8_t* image = nullptr;
    uint8_t* incoming = nullptr;
    lv_img_dsc_t descriptor {};
    lfs_file_t file {};
    bool open = false;
    size_t offset = 0;
    unsigned version = 0;
    char password[9] {};
  };

  PhotoBadge::PhotoBadge(Pinetime::Controllers::FS& fs, Pinetime::System::SystemTask& system)
    : RoundArtwork(200), portal(std::make_unique<PhotoPortal>(fs)), wakeLock(system) {
    controls = !HasPhoto();
  }

  PhotoBadge::~PhotoBadge() = default;

  bool PhotoBadge::Connected() const {
    return portal->Active();
  }

  bool PhotoBadge::HasPhoto() const {
    return portal->Image() != nullptr;
  }

  const char* PhotoBadge::Password() const {
    return portal->Password();
  }

  void PhotoBadge::Poll() {
    portal->Tick();
  }

  void PhotoBadge::Tap(int, int y) {
    if (controls && y >= 363) {
      if (portal->Active()) {
        portal->Stop();
        wakeLock.Release();
        controls = !HasPhoto();
      } else if (portal->Start())
        wakeLock.Lock();
    } else
      controls = !controls;
    Refresh();
  }

  void PhotoBadge::Draw(const lv_area_t* clip) {
    Paint p {clip};
    p.box(0, 0, 466, 466, 0x111e29);
    if (const auto* image = portal->Image()) {
      lv_draw_img_dsc_t d;
      lv_draw_img_dsc_init(&d);
      lv_area_t area {0, 0, 465, 465};
      lv_draw_img(&area, clip, image, &d);
    }
    if (!controls && HasPhoto())
      return;
    p.box(48, 80, 370, 339, 0x152a35, 36);
    p.text("P H O T O  B A D G E", 104, 0xa9ebd2);
    if (portal->Active()) {
      p.text("CONNECT PHONE TO", 157, 0x9ab5c3);
      p.text("InfiniTime-Badge", 190, 0xf2f0dc);
      p.text("PASSWORD", 226, 0x9ab5c3);
      p.text(portal->Password(), 256, 0xa9ebd2);
      p.text("192.168.4.1", 305, 0xf2f0dc);
      if (portal->State() == PhotoPortal::Transfer::Writing || portal->State() == PhotoPortal::Transfer::Receiving)
        p.text("SAVING...", 337, 0xa9ebd2);
    } else {
      p.text("YOUR FAVORITE MOMENT", 173, 0xf2f0dc);
      p.text("CROP ON YOUR PHONE", 214, 0x9ab5c3);
      p.text("KEEP IT WITH YOU", 250, 0x9ab5c3);
    }
    p.box(126, 369, 214, 42, 0xa9ebd2, 20);
    p.text(portal->Active() ? "CLOSE WI-FI" : "CONNECT PHONE", 380, 0x173330);
  }
}
