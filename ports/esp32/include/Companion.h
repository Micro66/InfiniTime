#pragma once
#include "CompanionProtocol.h"
#include "PhotoStore.h"

namespace Esp32::Companion {
  bool Init(PhotoStore& store);
  bool TakeCommand(CompanionProtocol::Command& command);
  void Publish(const CompanionProtocol::Packet& state);
  void Poll();
  bool Connected();
  unsigned NotificationFailures();
  bool Paired();
  bool TakePairingCode(uint32_t& code);
  void ForgetBonds();
}
