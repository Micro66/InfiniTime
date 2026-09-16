#include "PhotoBadge.h"
#include "PhotoPage.h"
#include "RoundPaint.h"
#include "CaptiveDns.h"
#include "qrcodegen.h"
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
#include <array>
#include <lwip/sockets.h>
#include <dhcpserver/dhcpserver.h>

namespace Esp32 {
  class PhotoPortal {
  public:
    static constexpr size_t FrameBytes = PhotoStore::FrameBytes;
    using Transfer = PhotoStore::State;

    explicit PhotoPortal(PhotoStore& store) : store(store) {
    }

    ~PhotoPortal() {
      Stop();
    }

    bool Start() {
      if (active)
        return true;
      snprintf(password, sizeof(password), "%08lx", static_cast<unsigned long>(esp_random()));
      char credentials[96];
      snprintf(credentials, sizeof(credentials), "WIFI:T:WPA;S:%s;P:%s;;", PhotoNetwork::Ssid, password);
      std::array<uint8_t, qrcodegen_BUFFER_LEN_FOR_VERSION(5)> temporary {};
      if (!qrcodegen_encodeText(credentials, temporary.data(), wifiCode.data(), qrcodegen_Ecc_MEDIUM, 1, 5, qrcodegen_Mask_AUTO, true) ||
          !qrcodegen_encodeText(PhotoNetwork::Url, temporary.data(), urlCode.data(), qrcodegen_Ecc_MEDIUM, 1, 5, qrcodegen_Mask_AUTO, true))
        return false;
      if (esp_netif_init() != ESP_OK)
        return false;
      const auto eventResult = esp_event_loop_create_default();
      eventLoopOwned = eventResult == ESP_OK;
      if (eventResult != ESP_OK && eventResult != ESP_ERR_INVALID_STATE)
        return false;
      netif = esp_netif_create_default_wifi_ap();
      // Advertise this AP's DNS before stations obtain their first DHCP lease.
      if (!netif || !StartDns()) {
        Stop();
        return false;
      }
      wifi_init_config_t wifiInit = WIFI_INIT_CONFIG_DEFAULT();
      radioInitialized = esp_wifi_init(&wifiInit) == ESP_OK;
      wifi_config_t wifi {};
      strcpy(reinterpret_cast<char*>(wifi.ap.ssid), PhotoNetwork::Ssid);
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
      config.uri_match_fn = httpd_uri_match_wildcard;
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
      httpd_uri_t probe {};
      probe.uri = "/*";
      probe.method = HTTP_GET;
      probe.handler = Redirect;
      if (httpd_register_uri_handler(server, &page) != ESP_OK || httpd_register_uri_handler(server, &frame) != ESP_OK ||
          httpd_register_uri_handler(server, &probe) != ESP_OK) {
        Stop();
        return false;
      }
      return true;
    }

    void Stop() {
      active = false;
      if (dnsSocket >= 0) {
        close(dnsSocket);
        dnsSocket = -1;
      }
      clients = 0;
      if (server) {
        httpd_stop(server);
        server = nullptr;
      }
      store.Cancel(PhotoStore::Owner::Wifi);
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
      if (active) {
        PollDns();
        if (millis() - lastClientPoll >= 500) {
          lastClientPoll = millis();
          wifi_sta_list_t stations {};
          if (esp_wifi_ap_get_sta_list(&stations) == ESP_OK)
            clients = stations.num;
        }
      }
    }

    const lv_img_dsc_t* Image() {
      return store.Image();
    }

    bool Active() const {
      return active;
    }

    const uint8_t* Code(bool page) const {
      return page ? urlCode.data() : wifiCode.data();
    }

    unsigned Clients() const {
      return clients;
    }

    unsigned DnsQueries() const {
      return dnsQueries;
    }

    Transfer State() const {
      return store.GetState();
    }

  private:
    bool StartDns() {
      const auto stopped = esp_netif_dhcps_stop(netif);
      if (stopped != ESP_OK && stopped != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED)
        return false;
      esp_netif_dns_info_t dns {};
      dns.ip.type = ESP_IPADDR_TYPE_V4;
      dns.ip.u_addr.ip4.addr = htonl(PhotoNetwork::Address);
      uint8_t offer = OFFER_DNS;
      if (esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns) != ESP_OK ||
          esp_netif_dhcps_option(netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer, sizeof(offer)) != ESP_OK ||
          esp_netif_dhcps_start(netif) != ESP_OK)
        return false;
      dnsSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
      sockaddr_in local {};
      local.sin_family = AF_INET;
      local.sin_port = htons(53);
      local.sin_addr.s_addr = htonl(INADDR_ANY);
      dnsQueries = 0;
      return dnsSocket >= 0 && bind(dnsSocket, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == 0;
    }

    void PollDns() {
      // Bounded, nonblocking work on the existing main loop; no additional task.
      uint8_t query[512], reply[528];
      for (unsigned i = 0; i < 4; ++i) {
        sockaddr_in peer {};
        socklen_t peerSize = sizeof(peer);
        const auto count = recvfrom(dnsSocket, query, sizeof(query), MSG_DONTWAIT, reinterpret_cast<sockaddr*>(&peer), &peerSize);
        if (count <= 0)
          break;
        const auto size = PhotoNetwork::DnsReply(query, count, reply, sizeof(reply));
        if (size) {
          sendto(dnsSocket, reply, size, MSG_DONTWAIT, reinterpret_cast<sockaddr*>(&peer), peerSize);
          ++dnsQueries;
        }
      }
    }

    static esp_err_t Redirect(httpd_req_t* request) {
      httpd_resp_set_status(request, "302 Found");
      httpd_resp_set_hdr(request, "Location", PhotoNetwork::Url);
      httpd_resp_set_hdr(request, "Cache-Control", "no-store");
      return httpd_resp_sendstr(request, "Open the local Photo Badge page.");
    }

    static esp_err_t Home(httpd_req_t* request) {
      httpd_resp_set_type(request, "text/html; charset=utf-8");
      httpd_resp_set_hdr(request, "Cache-Control", "no-store");
      return httpd_resp_send(request, PhotoPage, HTTPD_RESP_USE_STRLEN);
    }

    static esp_err_t Upload(httpd_req_t* request) {
      auto& self = *static_cast<PhotoPortal*>(request->user_ctx);
      auto& store = self.store;
      if (request->content_len != FrameBytes)
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Expected a 466 x 466 RGB565 frame");
      char crcText[10] {};
      char* end = nullptr;
      if (httpd_req_get_hdr_value_str(request, "X-CRC32", crcText, sizeof(crcText)) != ESP_OK || strlen(crcText) != 8)
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Missing checksum");
      const uint32_t expected = strtoul(crcText, &end, 16);
      if (*end)
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid checksum");
      const uint32_t id = esp_random() | 1;
      if (!store.Begin(PhotoStore::Owner::Wifi, id, FrameBytes, expected))
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Receiver busy or image memory unavailable");
      auto fail = [&](const char* message) {
        store.Cancel(PhotoStore::Owner::Wifi, id);
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, message);
      };
      uint8_t buffer[2048];
      size_t total = 0;
      while (self.active && total < FrameBytes) {
        const int count = httpd_req_recv(request, reinterpret_cast<char*>(buffer), std::min<size_t>(sizeof(buffer), FrameBytes - total));
        if (count <= 0 || !store.Append(PhotoStore::Owner::Wifi, id, total, buffer, count))
          return fail("Transfer interrupted; old image retained");
        total += count;
      }
      if (!self.active || !store.Commit(PhotoStore::Owner::Wifi, id))
        return fail("Transfer incomplete or checksum mismatch; old image retained");
      while (self.active && store.Busy())
        vTaskDelay(pdMS_TO_TICKS(10));
      if (store.GetState() != Transfer::Done || CompanionProtocol::U32(store.Status().data() + 4) != id)
        return fail("Save not confirmed; old image retained until commit");
      return httpd_resp_sendstr(request, "Saved");
    }

    PhotoStore& store;
    httpd_handle_t server = nullptr;
    esp_netif_t* netif = nullptr;
    bool radioInitialized = false, eventLoopOwned = false;
    std::atomic<bool> active {false};
    int dnsSocket = -1;
    unsigned clients = 0, dnsQueries = 0;
    uint32_t lastClientPoll = 0;
    std::array<uint8_t, qrcodegen_BUFFER_LEN_FOR_VERSION(5)> wifiCode {}, urlCode {};
    char password[9] {};
  };

  PhotoBadge::PhotoBadge(PhotoStore& store, Pinetime::System::SystemTask& system)
    : RoundArtwork(200), portal(std::make_unique<PhotoPortal>(store)), wakeLock(system) {
    controls = !HasPhoto();
  }

  PhotoBadge::~PhotoBadge() = default;

  bool PhotoBadge::Connected() const {
    return portal->Active();
  }

  bool PhotoBadge::HasPhoto() const {
    return portal->Image() != nullptr;
  }

  unsigned PhotoBadge::Clients() const {
    return portal->Clients();
  }

  unsigned PhotoBadge::DnsQueries() const {
    return portal->DnsQueries();
  }

  bool PhotoBadge::PageCode() const {
    return pageCode;
  }

  void PhotoBadge::Poll() {
    portal->Tick();
    const auto transfer = portal->State();
    if (transfer != lastTransfer) {
      lastTransfer = transfer;
      if (transfer == PhotoStore::State::Done && !portal->Active())
        controls = false;
      Refresh();
    }
    if (lastClients != portal->Clients()) {
      lastClients = portal->Clients();
      pageCode = lastClients > 0;
      Refresh();
    }
  }

  void PhotoBadge::Tap(int, int y) {
    if (portal->Active()) {
      if (y >= 369) {
        portal->Stop();
        wakeLock.Release();
        controls = !HasPhoto();
      } else if (y >= 333)
        pageCode = !pageCode;
    } else if (controls && y >= 369) {
      if (portal->Start()) {
        pageCode = false;
        lastClients = 0;
        wakeLock.Lock();
      }
    } else
      controls = !controls;
    Refresh();
  }

  void PhotoBadge::Draw(const lv_area_t* clip) {
    Paint p {clip};
    p.box(0, 0, 466, 466, 0x111e29);
    if (portal->Active()) {
      p.text(pageCode ? "2. OPEN PHOTO PAGE" : "1. SCAN TO JOIN", 49, 0xa9ebd2);
      const auto* code = portal->Code(pageCode);
      const int modules = qrcodegen_getSize(code);
      // Four white modules on every edge and integer scale preserve scan quality.
      const int scale = 246 / (modules + 8), side = (modules + 8) * scale;
      const int left = (466 - side) / 2, top = 209 - side / 2;
      p.box(left, top, side, side, 0xffffff);
      for (int y = 0; y < modules; ++y) {
        for (int x = 0; x < modules;) {
          if (!qrcodegen_getModule(code, x, y)) {
            ++x;
            continue;
          }
          const int begin = x++;
          while (x < modules && qrcodegen_getModule(code, x, y))
            ++x;
          p.box(left + (begin + 4) * scale, top + (y + 4) * scale, (x - begin) * scale, scale, 0x000000);
        }
      }
      p.box(126, 337, 214, 28, 0x284451, 14);
      p.text(pageCode ? "< WI-FI CODE" : "OPEN PAGE >", 339, 0xd5e9e5);
      p.box(126, 369, 214, 42, 0xa9ebd2, 20);
      const auto transfer = portal->State();
      p.text(transfer == PhotoPortal::Transfer::Writing || transfer == PhotoPortal::Transfer::Receiving ? "SAVING..." : "CLOSE WI-FI",
             380,
             0x173330);
      return;
    }
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
    p.text("YOUR FAVORITE MOMENT", 173, 0xf2f0dc);
    p.text("CROP ON YOUR PHONE", 214, 0x9ab5c3);
    p.text("KEEP IT WITH YOU", 250, 0x9ab5c3);
    p.box(126, 369, 214, 42, 0xa9ebd2, 20);
    p.text("CONNECT PHONE", 380, 0x173330);
  }
}
