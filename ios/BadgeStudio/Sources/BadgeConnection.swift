import Foundation
import CoreBluetooth
import Combine

@MainActor
final class BadgeConnection: NSObject, ObservableObject {
    struct Nearby: Identifiable {
        let peripheral: CBPeripheral
        let rssi: Int
        var id: UUID { peripheral.identifier }
        var name: String { peripheral.name ?? "InfiniTime Badge" }
    }
    @Published var nearby: [Nearby] = []
    @Published private(set) var ready = false
    @Published private(set) var scanning = false
    @Published private(set) var connecting = false
    @Published private(set) var connectionText = "正在启动蓝牙"
    @Published private(set) var state: DeviceState?
    @Published private(set) var firmware = "—"
    @Published private(set) var lastSync: Date?
    @Published private(set) var syncText = "连接后自动校时"
    @Published private(set) var message: String?
    @Published private(set) var working = false
    @Published private(set) var progress: Double = 0
    @Published private(set) var transferring = false
    @Published private(set) var transferText = ""
    var deviceName: String { peripheral?.name ?? "我的吧唧" }
    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var characteristics: [CBUUID: CBCharacteristic] = [:]
    private var remembered: UUID? = UserDefaults.standard.string(forKey: "badge.peripheral").flatMap(UUID.init(uuidString:))
    private var active = true
    private var automatic = true
    private var sequence: UInt16 = 0
    private struct Request {
        let sequence: UInt16
        let packet: Data
        let continuation: CheckedContinuation<Void, Error>
    }
    private var requests: [Request] = []
    private var current: Request?
    private var commandTimer: Timer?
    private var connectionTimer: Timer?
    private var transferTimer: Timer?
    private var progressPoll: Timer?
    private var lastPhotoState: UInt8 = 0
    var canCancelTransfer: Bool { transferring && !commitSent }
    private var frame = Data()
    private var transferID: UInt32 = 0
    private var sent = 0
    private var received = 0
    private var window = 8192
    private var commitSent = false
    private var started = false
    private var syncing = false

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: .main)
    }
    func foreground(_ value: Bool) {
        active = value
        if !value {
            central.stopScan(); scanning = false
            if transferring { cancelPhoto(reason: "传输已暂停：请保持 App 在前台，再重新发送。") }
        } else if ready { Task { await syncTime() } }
        else if !connecting { scan() }
    }
    func scan() {
        guard central.state == .poweredOn else { return }
        guard !ready, !connecting else { return }
        automatic = true; nearby = []; scanning = true; connectionText = "正在寻找附近的吧唧"
        central.scanForPeripherals(withServices: [CBUUID(string: Wire.service)], options: nil)
    }
    func connect(_ device: Nearby) {
        guard !connecting, !ready else { return }
        central.stopScan(); scanning = false; connecting = true; message = nil
        peripheral = device.peripheral; characteristics = [:]; state = nil
        device.peripheral.delegate = self
        connectionText = "正在连接，首次请按系统提示配对"
        central.connect(device.peripheral)
        connectionTimer?.invalidate()
        connectionTimer = Timer.scheduledTimer(withTimeInterval: 90, repeats: false) { [weak self] _ in
            Task { @MainActor in
                guard let self, self.connecting, let peripheral = self.peripheral else { return }
                self.automatic = false
                self.message = "配对超时。请解锁手机，重新连接，并输入吧唧显示的配对码。"
                self.central.cancelPeripheralConnection(peripheral)
            }
        }
    }
    func forget() {
        automatic = false; remembered = nil
        UserDefaults.standard.removeObject(forKey: "badge.peripheral")
        if let peripheral { central.cancelPeripheralConnection(peripheral) }
        central.stopScan(); scanning = false
        connectionText = "已忘记设备"
        message = "已清除 App 记忆。彻底解除配对，请同时在 iPhone 蓝牙设置和吧唧的 Pair iPhone 页面忘记设备。"
    }
    func clearMessage() { message = nil }
    func control(_ op: UInt8, _ value: UInt32, success: String? = nil) {
        guard ready, !working, !transferring else { return }
        working = true
        Task {
            defer { working = false }
            do { try await send(op, a: value); if let success { message = success } }
            catch { message = error.localizedDescription }
        }
    }
    func syncTime() async {
        guard ready, !syncing else { return }
        syncing = true; syncText = "正在同步时间与时区"
        defer { syncing = false }
        let date = Date()
        do {
            try await send(1, a: UInt32(date.timeIntervalSince1970), b: UInt32(bitPattern: Int32(TimeZone.current.secondsFromGMT(for: date))))
            guard let state, state.clockValid, abs(Date().timeIntervalSince1970 - Double(state.utc)) < 8 else {
                throw BadgeError.message("设备未确认正确时间，请重新校时。")
            }
            lastSync = Date(); syncText = "时间已与 iPhone 同步"
        } catch { syncText = "校时失败：\(error.localizedDescription)" }
    }
    private func send(_ op: UInt8, a: UInt32 = 0, b: UInt32 = 0, c: UInt32 = 0) async throws {
        guard ready else { throw BadgeError.message("设备尚未连接。") }
        sequence &+= 1
        if sequence == 0 { sequence = 1 }
        let seq = sequence
        try await withCheckedThrowingContinuation { continuation in
            requests.append(Request(sequence: seq, packet: Wire.command(op: op, sequence: seq, a: a, b: b, c: c), continuation: continuation))
            pumpCommand()
        }
    }
    private func pumpCommand() {
        guard current == nil, !requests.isEmpty, ready, let peripheral,
              let characteristic = characteristics[CBUUID(string: Wire.command)] else { return }
        current = requests.removeFirst()
        peripheral.writeValue(current!.packet, for: characteristic, type: .withResponse)
        commandTimer = Timer.scheduledTimer(withTimeInterval: 10, repeats: false) { [weak self] _ in
            Task { @MainActor in self?.completeCommand(BadgeError.message("设备没有确认操作，请重连后检查状态。")) }
        }
    }
    private func completeCommand(_ error: Error? = nil) {
        guard let request = current else { return }
        current = nil; commandTimer?.invalidate()
        if let error { request.continuation.resume(throwing: error) }
        else { request.continuation.resume() }
        pumpCommand()
    }
    func sendPhoto(_ data: Data) {
        guard ready, !transferring, !working, data.count == Wire.frameBytes else { return }
        transferring = true; progress = 0; transferText = "准备发送"; message = nil
        frame = data; transferID = UInt32.random(in: 1...UInt32.max)
        sent = 0; received = 0; window = 8192; commitSent = false; started = false
        lastPhotoState = 0
        armTransferTimeout()
        progressPoll?.invalidate()
        progressPoll = Timer.scheduledTimer(withTimeInterval: 2, repeats: true) { [weak self] _ in
            Task { @MainActor in
                guard let self, self.transferring, let peripheral = self.peripheral,
                      let characteristic = self.characteristics[CBUUID(string: Wire.photoState)] else { return }
                peripheral.readValue(for: characteristic)
            }
        }
        let token = transferID
        Task {
            do {
                try await send(7, a: UInt32(data.count), b: Wire.crc32(data), c: token)
                guard transferring, transferID == token else { return }
                started = true; transferText = "正在传输，请保持 App 在前台"
                pumpPhoto()
            } catch { if transferID == token { failPhoto(error.localizedDescription) } }
        }
    }
    private func pumpPhoto() {
        guard transferring, started, !commitSent, let peripheral,
              let characteristic = characteristics[CBUUID(string: Wire.photoData)] else { return }
        let packetSize = min(512, peripheral.maximumWriteValueLength(for: .withoutResponse)) - 8
        guard packetSize > 0 else { failPhoto("蓝牙包长度异常，请重连。"); return }
        while sent < frame.count && peripheral.canSendWriteWithoutResponse && sent - received < window {
            let count = min(packetSize, frame.count - sent, window - (sent - received))
            let packet = Wire.le(transferID) + Wire.le(UInt32(sent)) + frame.subdata(in: sent..<(sent + count))
            peripheral.writeValue(packet, for: characteristic, type: .withoutResponse)
            sent += count
        }
        if received == frame.count && !frame.isEmpty {
            commitSent = true; transferText = "正在校验并保存到吧唧"
            let token = transferID
            Task {
                do { try await send(8, a: token) }
                catch { if transferring && token == transferID { failPhoto(error.localizedDescription) } }
            }
        }
    }
    func cancelPhoto(reason: String = "已停止发送，原照片保留。") {
        guard transferring else { return }
        let token = transferID
        failPhoto(commitSent ? "设备可能已开始保存，请以设备画面为准。" : reason)
        Task {
            do { try await send(9, a: token) }
            catch { message = "已停止手机发送；设备取消未确认：\(error.localizedDescription)" }
        }
    }
    private func failPhoto(_ text: String) {
        transferring = false; transferText = text; frame = Data(); transferTimer?.invalidate(); progressPoll?.invalidate()
    }
    private func armTransferTimeout() {
        transferTimer?.invalidate()
        transferTimer = Timer.scheduledTimer(withTimeInterval: 25, repeats: false) { [weak self] _ in
            Task { @MainActor in self?.cancelPhoto(reason: "传输超时，未确认保存。请保持设备靠近后重试。") }
        }
    }
    private func resetConnection(_ text: String) {
        ready = false; connecting = false; state = nil; characteristics = [:]
        connectionTimer?.invalidate(); commandTimer?.invalidate()
        if transferring { failPhoto("连接中断，未确认保存。重新连接后可再次发送。") }
        let pending = requests; requests = []
        if let current { self.current = nil; current.continuation.resume(throwing: BadgeError.message(text)) }
        pending.forEach { $0.continuation.resume(throwing: BadgeError.message(text)) }
        connectionText = text; syncText = "重连后自动校时"
    }
    private func checkReady() {
        guard !ready, state != nil,
              characteristics[CBUUID(string: Wire.state)]?.isNotifying == true,
              characteristics[CBUUID(string: Wire.photoState)]?.isNotifying == true else { return }
        // Start after the device's last acknowledgement; an app restart must not
        // mistake a previous connection's ACK for its first new command.
        sequence = state?.ack ?? 0
        ready = true; connecting = false; connectionTimer?.invalidate(); connectionText = "蓝牙已连接"
        remembered = peripheral?.identifier
        UserDefaults.standard.set(remembered?.uuidString, forKey: "badge.peripheral")
        Task { await syncTime() }
    }
}

// CoreBluetooth delegates are delivered on the explicitly configured main queue.
extension BadgeConnection: @preconcurrency CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn: if active { scan() }
        case .poweredOff: resetConnection("请打开 iPhone 蓝牙"); scanning = false
        case .unauthorized: resetConnection("请在系统设置中允许“吧唧”使用蓝牙"); scanning = false
        case .unsupported: resetConnection("此设备不支持蓝牙"); scanning = false
        default: resetConnection("蓝牙正在准备中"); scanning = false
        }
    }
    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral, advertisementData: [String: Any], rssi RSSI: NSNumber) {
        let device = Nearby(peripheral: peripheral, rssi: RSSI.intValue)
        if let index = nearby.firstIndex(where: { $0.id == device.id }) { nearby[index] = device }
        else { nearby.append(device) }
        if automatic && remembered == peripheral.identifier { connect(device) }
    }
    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        connectionText = "正在读取设备，首次请完成系统配对"
        peripheral.discoverServices([CBUUID(string: Wire.service)])
    }
    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        automatic = false; resetConnection("连接失败：\(error?.localizedDescription ?? "请重新连接")")
    }
    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        let reconnect = automatic && active && remembered != nil
        resetConnection("设备已断开")
        if reconnect { scan() }
    }
}

extension BadgeConnection: @preconcurrency CBPeripheralDelegate {
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard error == nil, let service = peripheral.services?.first(where: { $0.uuid == CBUUID(string: Wire.service) }) else {
            message = "无法发现伴侣服务，请更新设备固件。"; automatic = false; central.cancelPeripheralConnection(peripheral); return
        }
        peripheral.discoverCharacteristics(nil, for: service)
    }
    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        guard error == nil else { message = error?.localizedDescription; central.cancelPeripheralConnection(peripheral); return }
        for value in service.characteristics ?? [] { characteristics[value.uuid] = value }
        guard [Wire.command, Wire.state, Wire.photoData, Wire.photoState, Wire.version].allSatisfy({ characteristics[CBUUID(string: $0)] != nil }) else {
            message = "设备固件的蓝牙接口不完整，请更新固件。"; automatic = false; central.cancelPeripheralConnection(peripheral); return
        }
        for id in [Wire.state, Wire.photoState] { peripheral.setNotifyValue(true, for: characteristics[CBUUID(string: id)]!) }
        peripheral.readValue(for: characteristics[CBUUID(string: Wire.state)]!)
        peripheral.readValue(for: characteristics[CBUUID(string: Wire.version)]!)
    }
    func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic, error: Error?) {
        if let error { message = "配对或订阅失败：\(error.localizedDescription)"; automatic = false; central.cancelPeripheralConnection(peripheral); return }
        checkReady()
    }
    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        if let error { message = error.localizedDescription; return }
        guard let data = characteristic.value else { return }
        switch characteristic.uuid {
        case CBUUID(string: Wire.state):
            guard let value = DeviceState(data) else { message = "设备状态格式不兼容，请更新 App 与固件。"; return }
            state = value
            if let current, current.sequence == value.ack {
                let reasons = [1: "设备拒绝了无效设置。", 2: "设备正在忙，请稍后重试。", 3: "设备执行失败，未确认保存。"]
                completeCommand(value.result == 0 ? nil : BadgeError.message(reasons[Int(value.result)] ?? "设备返回未知错误。"))
            }
            checkReady()
        case CBUUID(string: Wire.photoState):
            guard transferring, let status = PhotoState(data), status.id == transferID else { return }
            guard status.received >= received, status.received <= sent else { cancelPhoto(reason: "传输确认顺序异常，已停止发送。"); return }
            if status.received > received || status.state != lastPhotoState { armTransferTimeout() }
            lastPhotoState = status.state
            received = status.received; window = status.window; progress = Double(received) / Double(Wire.frameBytes)
            if status.state == 4 {
                guard commitSent, received == Wire.frameBytes else { cancelPhoto(reason: "设备保存确认异常。"); return }
                transferring = false; transferTimer?.invalidate(); progressPoll?.invalidate(); frame = Data(); transferText = "已保存到吧唧"
            } else if status.state == 5 { failPhoto("设备未保存（错误 \(status.error)）。请重新发送。") }
            else { pumpPhoto() }
        case CBUUID(string: Wire.version): firmware = String(data: data, encoding: .utf8) ?? "未知版本"
        default: break
        }
    }
    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        if characteristic.uuid == CBUUID(string: Wire.command), let error { completeCommand(error) }
    }
    func peripheralIsReady(toSendWriteWithoutResponse peripheral: CBPeripheral) { pumpPhoto() }
}
