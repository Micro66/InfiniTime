#include "Companion.h"
#include <BLEDevice.h>
#include <BLE2902.h>
#include <esp_random.h>
#include <freertos/queue.h>
#include <atomic>
#include <vector>
#include <mutex>

namespace Esp32::Companion {
  namespace {
    using namespace CompanionProtocol;
    PhotoStore* photos = nullptr;
    QueueHandle_t commands = nullptr;
    BLEServer* server = nullptr;
    BLECharacteristic *stateCharacteristic = nullptr, *photoCharacteristic = nullptr;
    std::atomic<bool> connected {false}, authenticated {false}, restartAdvertising {false};
    std::atomic<uint32_t> pairingCode {UINT32_MAX};
    std::atomic<uint16_t> connectionId {0xffff};
    std::atomic<uint32_t> overflow {0};
    uint32_t lastNotify = 0;
    Packet previousPhoto {}, deviceSnapshot {};
    std::mutex snapshotMutex;
    std::atomic<esp_gatt_if_t> serverInterface {ESP_GATT_IF_NONE};
    std::atomic<bool> stateSubscribed {false}, photoSubscribed {false};
    std::atomic<unsigned> notificationFailures {0};

    void GattEvent(esp_gatts_cb_event_t event, esp_gatt_if_t interface, esp_ble_gatts_cb_param_t* param) {
      if (event == ESP_GATTS_REG_EVT && param->reg.status == ESP_GATT_OK)
        serverInterface = interface;
    }

    bool Notify(BLECharacteristic* characteristic, Packet packet, bool subscribed) {
      if (!authenticated || !subscribed)
        return false;
      // The IDF call copies packet bytes. Do not share Arduino String values or
      // iterate BLEServer's mutable peer map from the main task.
      if (esp_ble_gatts_send_indicate(serverInterface, connectionId, characteristic->getHandle(), packet.size(), packet.data(), false) ==
          ESP_OK)
        return true;
      ++notificationFailures;
      return false;
    }

    class StateReads final : public BLECharacteristicCallbacks {
      void onRead(BLECharacteristic* characteristic) override {
        Packet packet;
        {
          std::lock_guard lock(snapshotMutex);
          packet = deviceSnapshot;
        }
        characteristic->setValue(packet.data(), packet.size());
      }
    } stateReads;

    class PhotoReads final : public BLECharacteristicCallbacks {
      void onRead(BLECharacteristic* characteristic) override {
        auto packet = photos->Status();
        characteristic->setValue(packet.data(), packet.size());
      }
    } photoReads;

    class Subscription final : public BLEDescriptorCallbacks {
    public:
      explicit Subscription(std::atomic<bool>& flag) : flag(flag) {
      }

      void onWrite(BLEDescriptor* descriptor) override {
        flag = descriptor->getLength() == 2 && (descriptor->getValue()[0] & 1);
      }

    private:
      std::atomic<bool>& flag;
    } stateSubscription(stateSubscribed), photoSubscription(photoSubscribed);

    class Security final : public BLESecurityCallbacks {
      uint32_t onPassKeyRequest() override {
        return 0;
      }

      void onPassKeyNotify(uint32_t code) override {
        pairingCode = code;
      }

      bool onSecurityRequest() override {
        return true;
      }

      bool onConfirmPIN(uint32_t) override {
        return false;
      }

      void onAuthenticationComplete(esp_ble_auth_cmpl_t result) override {
        authenticated = result.success;
        if (!result.success && server)
          server->disconnect(connectionId);
      }
    } securityCallbacks;

    class Connections final : public BLEServerCallbacks {
      void onConnect(BLEServer* srv, esp_ble_gatts_cb_param_t* param) override {
        if (connected.exchange(true)) {
          srv->disconnect(param->connect.conn_id);
          return;
        }
        connectionId = param->connect.conn_id;
        authenticated = false;
        stateSubscribed = photoSubscribed = false;
        srv->updateConnParams(param->connect.remote_bda, 12, 24, 0, 400);
      }

      void onDisconnect(BLEServer*, esp_ble_gatts_cb_param_t* param) override {
        if (param->disconnect.conn_id != connectionId)
          return;
        authenticated = connected = false;
        stateSubscribed = photoSubscribed = false;
        connectionId = 0xffff;
        pairingCode = UINT32_MAX;
        xQueueReset(commands);
        photos->Cancel(PhotoStore::Owner::Ble);
        restartAdvertising = true;
      }
    } connectionCallbacks;

    class Writes final : public BLECharacteristicCallbacks {
      void onWrite(BLECharacteristic* characteristic) override {
        if (!authenticated)
          return;
        const auto value = characteristic->getValue();
        const auto* data = reinterpret_cast<const uint8_t*>(value.c_str());
        Command command;
        if (!Decode(data, value.length(), command)) {
          command.op = 0;
          if (value.length() >= 4)
            command.sequence = data[2] | data[3] << 8;
        }
        if (xQueueSend(commands, &command, 0) != pdTRUE)
          overflow = uint32_t(command.sequence) + 1;
      }
    } commandCallbacks;

    class DataWrites final : public BLECharacteristicCallbacks {
      void onWrite(BLECharacteristic* characteristic) override {
        if (!authenticated)
          return;
        const auto value = characteristic->getValue();
        if (value.length() <= 8)
          return;
        const auto* data = reinterpret_cast<const uint8_t*>(value.c_str());
        photos->Append(PhotoStore::Owner::Ble, U32(data), U32(data + 4), data + 8, value.length() - 8);
      }
    } dataCallbacks;

    BLECharacteristic*
    Characteristic(BLEService* service, const char* uuid, uint32_t properties, BLEDescriptorCallbacks* subscription = nullptr) {
      auto* c = service->createCharacteristic(uuid, properties);
      c->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM | ESP_GATT_PERM_WRITE_ENC_MITM);
      if (subscription) {
        auto* descriptor = new BLE2902();
        descriptor->setCallbacks(subscription);
        descriptor->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM | ESP_GATT_PERM_WRITE_ENC_MITM);
        c->addDescriptor(descriptor);
      }
      return c;
    }
  }

  bool Init(PhotoStore& store) {
    photos = &store;
    commands = xQueueCreate(8, sizeof(Command));
    if (!commands)
      return false;
    BLEDevice::init("InfiniTime Badge");
    if (!BLEDevice::getInitialized())
      return false;
    BLEDevice::setMTU(247);
    BLEDevice::setSecurityCallbacks(&securityCallbacks);
    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT_MITM);
    BLESecurity security;
    security.setStaticPIN(100000 + esp_random() % 900000);
    security.setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
    security.setCapability(ESP_IO_CAP_OUT);
    security.setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    BLEDevice::setCustomGattsHandler(GattEvent);
    server = BLEDevice::createServer();
    server->setCallbacks(&connectionCallbacks);
    auto* service = server->createService(BLEUUID(Service), 24);
    Characteristic(service, CommandId, BLECharacteristic::PROPERTY_WRITE)->setCallbacks(&commandCallbacks);
    Characteristic(service, DataId, BLECharacteristic::PROPERTY_WRITE_NR)->setCallbacks(&dataCallbacks);
    stateCharacteristic =
      Characteristic(service, StateId, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY, &stateSubscription);
    photoCharacteristic =
      Characteristic(service, PhotoId, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY, &photoSubscription);
    stateCharacteristic->setCallbacks(&stateReads);
    photoCharacteristic->setCallbacks(&photoReads);
    Characteristic(service, VersionId, BLECharacteristic::PROPERTY_READ)->setValue("Badge Studio 1.1 / protocol 1");
    service->start();
    auto* advertising = BLEDevice::getAdvertising();
    advertising->addServiceUUID(Service);
    advertising->setScanResponse(true);
    advertising->start();
    return true;
  }

  bool TakeCommand(Command& command) {
    if (!commands)
      return false;
    if (xQueueReceive(commands, &command, 0) == pdTRUE)
      return true;
    const auto rejected = overflow.exchange(0);
    if (!rejected)
      return false;
    command = {};
    command.op = 255;
    command.sequence = rejected - 1;
    return true;
  }

  void Publish(const Packet& state) {
    if (!stateCharacteristic)
      return;
    {
      std::lock_guard lock(snapshotMutex);
      deviceSnapshot = state;
    }
    Notify(stateCharacteristic, state, stateSubscribed);
  }

  void Poll() {
    if (restartAdvertising.exchange(false))
      BLEDevice::startAdvertising();
    if (!photoCharacteristic || millis() - lastNotify < 60)
      return;
    lastNotify = millis();
    auto state = photos->Status();
    if (state != previousPhoto && Notify(photoCharacteristic, state, photoSubscribed))
      previousPhoto = state;
  }

  unsigned NotificationFailures() {
    return notificationFailures;
  }

  bool Connected() {
    return connected;
  }

  bool Paired() {
    return authenticated;
  }

  bool TakePairingCode(uint32_t& code) {
    code = pairingCode.exchange(UINT32_MAX);
    return code != UINT32_MAX;
  }

  void ForgetBonds() {
    if (!server)
      return;
    if (connected)
      server->disconnect(connectionId);
    int count = esp_ble_get_bond_device_num();
    std::vector<esp_ble_bond_dev_t> bonds(count);
    if (count && esp_ble_get_bond_device_list(&count, bonds.data()) == ESP_OK)
      for (auto& bond : bonds)
        esp_ble_remove_bond_device(bond.bd_addr);
  }
}
