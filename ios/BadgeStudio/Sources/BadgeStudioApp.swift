import SwiftUI

enum Palette {
    static let background = Color(red: 0.055, green: 0.067, blue: 0.10)
    static let card = Color(red: 0.10, green: 0.12, blue: 0.17)
    static let accent = Color(red: 0.73, green: 0.70, blue: 1)
    static let mint = Color(red: 0.57, green: 0.90, blue: 0.76)
    static let muted = Color(red: 0.62, green: 0.66, blue: 0.75)
}

@main
struct BadgeStudioApp: App {
    @StateObject private var badge = BadgeConnection()
    @Environment(\.scenePhase) private var phase
    var body: some Scene {
        WindowGroup {
            TabView {
                NavigationStack { DeviceView() }.tabItem { Label("设备", systemImage: "circle.hexagongrid.fill") }
                NavigationStack { StylesView() }.tabItem { Label("装扮", systemImage: "sparkles") }
                NavigationStack { SettingsView() }.tabItem { Label("设置", systemImage: "slider.horizontal.3") }
            }
            .tint(Palette.accent).preferredColorScheme(.dark).environmentObject(badge)
            .onChange(of: phase) { _, value in badge.foreground(value == .active) }
            .alert("吧唧", isPresented: Binding(get: { badge.message != nil }, set: { if !$0 { badge.clearMessage() } })) {
                Button("知道了") { badge.clearMessage() }
            } message: { Text(badge.message ?? "") }
        }
    }
}

struct Card<Content: View>: View {
    @ViewBuilder let content: Content
    var body: some View { content.padding(20).frame(maxWidth: .infinity, alignment: .leading).background(Palette.card, in: RoundedRectangle(cornerRadius: 24)).contentShape(RoundedRectangle(cornerRadius: 24)) }
}

struct DeviceView: View {
    @EnvironmentObject private var badge: BadgeConnection
    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 22) {
                HStack {
                    VStack(alignment: .leading, spacing: 6) {
                        Text("BADGE STUDIO").font(.caption.weight(.semibold)).tracking(3).foregroundStyle(Palette.accent)
                        Text("我的吧唧").font(.system(size: 34, weight: .bold, design: .rounded))
                    }
                    Spacer()
                    if let state = badge.state {
                        Label("\(state.battery)%", systemImage: state.charging ? "battery.100percent.bolt" : "battery.75percent")
                            .font(.subheadline.monospacedDigit()).foregroundStyle(Palette.mint)
                    }
                }
                ZStack {
                    Circle().fill(RadialGradient(colors: [Palette.accent.opacity(0.16), .clear], center: .center, startRadius: 70, endRadius: 160))
                    Image(preview).resizable().scaledToFit().frame(width: 240, height: 240).clipShape(Circle())
                        .overlay(Circle().stroke(.white.opacity(0.12), lineWidth: 7))
                        .shadow(color: .black.opacity(0.6), radius: 24, y: 16)
                }.frame(maxWidth: .infinity).frame(height: 292)
                    .accessibilityLabel("当前样式示意图，非实时屏幕")
                    .overlay(alignment: .bottom) { Text("样式预览").font(.caption2).foregroundStyle(Palette.muted) }
                HStack(spacing: 7) {
                    Circle().fill(badge.ready ? Palette.mint : Palette.muted).frame(width: 7, height: 7)
                    Text(badge.connectionText).font(.subheadline).foregroundStyle(Palette.muted)
                    Spacer()
                    if badge.connecting { ProgressView().tint(Palette.accent) }
                }
                if badge.ready {
                    Card {
                        HStack(alignment: .top, spacing: 14) {
                            Image(systemName: "clock.badge.checkmark").font(.title2).foregroundStyle(Palette.mint)
                            VStack(alignment: .leading, spacing: 6) {
                                Text(badge.syncText).font(.headline)
                                if let state = badge.state, state.clockValid {
                                    Text(deviceTime(state)).font(.subheadline.monospacedDigit()).foregroundStyle(Palette.muted)
                                }
                                if let date = badge.lastSync { Text("最近校时 \(date.formatted(date: .omitted, time: .shortened))").font(.caption).foregroundStyle(Palette.muted) }
                            }
                            Spacer(minLength: 0)
                            Button { Task { await badge.syncTime() } } label: { Image(systemName: "arrow.clockwise").frame(width: 44, height: 44).contentShape(Rectangle()) }.accessibilityLabel("立即校时")
                        }
                    }
                    HStack(spacing: 12) {
                        quick(badge.state?.asleep == true ? "亮屏" : "熄屏", "power") { badge.control(2, badge.state?.asleep == true ? 1 : 0) }
                        quick("回到表盘", "clock") { badge.control(5, UInt32(badge.state?.watch ?? 0)) }
                    }.disabled(badge.working || badge.transferring)
                    NavigationLink { PhotoEditor() } label: {
                        Card {
                            HStack {
                                Image(systemName: "photo.on.rectangle.angled").font(.title2).foregroundStyle(Palette.accent)
                                VStack(alignment: .leading, spacing: 6) {
                                    Text("换上喜欢的照片").font(.headline).foregroundStyle(.white)
                                    Text("选图 · 裁剪 · 蓝牙直传").font(.caption).foregroundStyle(Palette.muted)
                                }
                                Spacer(); Image(systemName: "arrow.up.right").foregroundStyle(Palette.accent)
                            }
                        }
                    }.buttonStyle(.plain)
                } else {
                    Card {
                        VStack(alignment: .leading, spacing: 16) {
                            Text(badge.connecting ? "第一次，认识一下。" : "让吧唧靠近一点。").font(.title3.bold())
                            Text("打开设备电源，选择下方设备。首次连接时，将吧唧上的六位码输入 iPhone 配对框。").font(.subheadline).foregroundStyle(Palette.muted)
                            ForEach(badge.nearby) { device in
                                Button { badge.connect(device) } label: {
                                    HStack { Image(systemName: "circle.dotted"); Text(device.name); Spacer(); Text("连接").font(.caption.bold()) }
                                        .padding(14).background(.white.opacity(0.05), in: RoundedRectangle(cornerRadius: 14))
                                        .contentShape(RoundedRectangle(cornerRadius: 14))
                                }.disabled(badge.connecting)
                            }
                            if !badge.connecting {
                                Button { badge.scan() } label: {
                                    Text(badge.scanning ? "重新搜索" : "搜索设备").font(.headline)
                                        .frame(maxWidth: .infinity).frame(minHeight: 48)
                                        .background(Palette.accent.opacity(0.12), in: RoundedRectangle(cornerRadius: 14))
                                        .contentShape(RoundedRectangle(cornerRadius: 14))
                                }.buttonStyle(.plain)
                            }
                            Text("需要配套的 Badge Studio 固件。").font(.caption).foregroundStyle(Palette.muted)
                        }
                    }
                }
                Text("属于你的一小块屏幕。")
                    .font(.caption).foregroundStyle(Palette.muted).frame(maxWidth: .infinity).padding(.top, 6)
            }.padding(24)
        }.background(Palette.background).toolbar(.hidden, for: .navigationBar)
    }
    private var preview: String {
        guard let state = badge.state else { return "badge-mochi" }
        if state.page == 10 { return StylesView.characters[state.theme].asset }
        return StylesView.watches[state.watch].asset
    }
    private func quick(_ title: String, _ icon: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            HStack { Image(systemName: icon); Text(title).font(.headline) }.frame(maxWidth: .infinity).padding(20).contentShape(RoundedRectangle(cornerRadius: 22))
        }.buttonStyle(.plain).background(Palette.card, in: RoundedRectangle(cornerRadius: 22)).foregroundStyle(.white)
    }
    private func deviceTime(_ state: DeviceState) -> String {
        let format = DateFormatter(); format.timeZone = TimeZone(secondsFromGMT: Int(state.zone)); format.dateFormat = "MM月dd日 HH:mm:ss"
        return format.string(from: Date(timeIntervalSince1970: Double(state.utc)))
    }
}

struct StylesView: View {
    struct Style: Identifiable {
        let id: Int; let name: String; let subtitle: String; let asset: String
    }
    static let watches = [Style(id: 0, name: "数字", subtitle: "清晰而直接", asset: "digital"), Style(id: 1, name: "经典", subtitle: "熟悉的指针", asset: "analog"),
                          Style(id: 2, name: "Orbit", subtitle: "时间的轨道", asset: "orbit"), Style(id: 3, name: "Studio", subtitle: "温暖的刻度", asset: "studio"), Style(id: 4, name: "Pulse", subtitle: "跟着节奏走", asset: "pulse")]
    static let characters = [Style(id: 0, name: "Mochi", subtitle: "软乎乎的猫咪", asset: "badge-mochi"), Style(id: 1, name: "Beep", subtitle: "你的机器人朋友", asset: "badge-beep"), Style(id: 2, name: "Lil’ Orbit", subtitle: "一颗小小星球", asset: "badge-little-orbit")]
    @EnvironmentObject private var badge: BadgeConnection
    @State private var category = 0
    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 22) {
                Text("今天，换个样子。").font(.system(size: 30, weight: .bold, design: .rounded))
                Text("给时间一点个性，也给自己一点开心。").foregroundStyle(Palette.muted).font(.subheadline)
                Picker("装扮类型", selection: $category) { Text("表盘").tag(0); Text("角色").tag(1) }.pickerStyle(.segmented)
                LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())], spacing: 14) {
                    ForEach(category == 0 ? Self.watches : Self.characters) { style in
                        Button { badge.control(category == 0 ? 5 : 6, UInt32(style.id)) } label: {
                            VStack(alignment: .leading, spacing: 10) {
                                Image(style.asset).resizable().scaledToFit().clipShape(Circle()).padding(4)
                                HStack { Text(style.name).font(.headline); Spacer(minLength: 0); if selected(style.id) { Image(systemName: "checkmark.circle.fill").foregroundStyle(Palette.mint) } }
                                Text(style.subtitle).font(.caption).foregroundStyle(Palette.muted)
                            }.padding(14).background(Palette.card, in: RoundedRectangle(cornerRadius: 24))
                                .contentShape(RoundedRectangle(cornerRadius: 24))
                        }.buttonStyle(.plain).disabled(!badge.ready || badge.working || badge.transferring)
                    }
                }
                NavigationLink { PhotoEditor() } label: {
                    HStack { Image(systemName: "photo.badge.plus"); Text("制作照片吧唧").font(.headline); Spacer(); Image(systemName: "chevron.right") }
                        .padding(22).background(Palette.accent.opacity(0.13), in: RoundedRectangle(cornerRadius: 22))
                        .contentShape(RoundedRectangle(cornerRadius: 22))
                }.buttonStyle(.plain).foregroundStyle(Palette.accent)
                Text(badge.ready ? "预览为样式示意，点选后以设备确认结果为准。" : "连接吧唧后，即可应用喜欢的样式。")
                    .font(.caption).foregroundStyle(Palette.muted)
            }.padding(24)
        }.background(Palette.background).navigationTitle("装扮").navigationBarTitleDisplayMode(.inline)
    }
    private func selected(_ id: Int) -> Bool {
        guard badge.ready, let state = badge.state else { return false }
        return category == 0 ? state.watch == id : state.page == 10 && state.theme == id
    }
}

struct SettingsView: View {
    @EnvironmentObject private var badge: BadgeConnection
    @State private var confirmForget = false
    var body: some View {
        Form {
            Section("显示") {
                Picker("屏幕亮度", selection: Binding(get: { badge.state?.brightness ?? 3 }, set: { badge.control(3, UInt32($0)) })) {
                    Text("柔和").tag(2); Text("标准").tag(3); Text("明亮").tag(4)
                }
                Picker("自动熄屏", selection: Binding(get: { badge.state?.timeout ?? 30 }, set: { badge.control(4, UInt32($0)) })) {
                    Text("15 秒").tag(15); Text("30 秒").tag(30); Text("1 分钟").tag(60); Text("2 分钟").tag(120); Text("5 分钟").tag(300)
                }
            }.disabled(!badge.ready || badge.working || badge.transferring)
            Section {
                LabeledContent("同步状态", value: badge.syncText)
                Button("立即同步时间与时区") { Task { await badge.syncTime() } }.disabled(!badge.ready)
            } header: { Text("时间") } footer: {
                Text("连接和回到 App 时自动校时。设备断开后继续走时；彻底断电后需重新同步。时区与夏令时在下一次连接时更新。")
            }
            Section("打开设备应用") {
                appButton("角色吧唧", icon: "face.smiling", page: 10)
                appButton("照片吧唧", icon: "photo", page: 15)
                appButton("幸运骰子", icon: "die.face.5", page: 11)
                appButton("重力弹球", icon: "circle.grid.cross", page: 12)
                appButton("Sound Buddy", icon: "waveform", page: 13)
                appButton("专注花园", icon: "leaf", page: 14)
            }.disabled(!badge.ready || badge.working || badge.transferring)
            Section("关于设备") {
                LabeledContent("型号", value: "Waveshare 1.75C")
                LabeledContent("固件", value: badge.firmware)
                LabeledContent("连接方式", value: "加密蓝牙 BLE")
                LabeledContent("App 版本", value: "1.0.0")
                Button("忘记此设备", role: .destructive) { confirmForget = true }
            }
            Section { Text("照片只保存在你的手机和吧唧，不上传服务器。App 不申请后台常驻，也不读取整个相册。").font(.footnote).foregroundStyle(Palette.muted) }
        }.scrollContentBackground(.hidden).background(Palette.background).navigationTitle("设置")
            .confirmationDialog("忘记此设备？", isPresented: $confirmForget, titleVisibility: .visible) {
                Button("忘记设备", role: .destructive) { badge.forget() }
            } message: { Text("App 将停止自动重连。设备上的照片和设置不会删除。") }
    }
    private func appButton(_ name: String, icon: String, page: UInt32) -> some View {
        Button { badge.control(10, page) } label: { Label(name, systemImage: icon) }.foregroundStyle(.white)
    }
}
