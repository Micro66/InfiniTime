#pragma once
#include "RoundArtwork.h"
#include "components/fs/FS.h"
#include <memory>

namespace Esp32 {
  class PhotoPortal;

  class PhotoBadge : public RoundArtwork {
  public:
    PhotoBadge(Pinetime::Controllers::FS& fs, Pinetime::System::SystemTask& system);
    ~PhotoBadge() override;
    void Poll();
    bool Connected() const;
    bool HasPhoto() const;
    unsigned Clients() const;
    unsigned DnsQueries() const;
    bool PageCode() const;

  private:
    void Draw(const lv_area_t*) override;
    void Tap(int x, int y) override;
    std::unique_ptr<PhotoPortal> portal;
    Pinetime::System::WakeLock wakeLock;
    bool controls = true;
    bool pageCode = false;
    unsigned lastClients = 0;
  };
}
