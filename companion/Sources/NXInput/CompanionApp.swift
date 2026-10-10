import SwiftUI
import AppKit
import ApplicationServices
import Network
import CompanionCore

struct TargetDisplay: Identifiable, Equatable {
    let id: CGDirectDisplayID
    let name: String
}
@MainActor final class Companion: ObservableObject {
    @Published var address = UserDefaults.standard.string(forKey: "SwitchAddress") ?? ""
    @Published var code = ""
    @Published var displays: [TargetDisplay] = []
    @Published var target: CGDirectDisplayID = 0
    @Published var connected = false
    @Published var connecting = false
    @Published var status = "Connect OpenDisplay Sender to your Switch first."
    @Published var trusted = AXIsProcessTrusted()
    private var connection: NWConnection?
    private var framer = Framer()
    private let injector = Injector()
    private var heartbeat: Timer?
    private var lastReply = Date()
    private var refreshTimer: Timer?
    init() {
        refreshDisplays()
        refreshTimer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.refreshDisplays() }
        }
    }
    func refreshDisplays() {
        trusted = AXIsProcessTrusted()
        let fresh = NSScreen.screens.compactMap { screen -> TargetDisplay? in
            guard let number = screen.deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")] as? NSNumber else { return nil }
            return TargetDisplay(id: number.uint32Value, name: screen.localizedName)
        }
        if displays != fresh { displays = fresh }
        if connected || connecting {
            if !displays.contains(where: { $0.id == target }) { disconnect("Target display disconnected.") }
        } else if !displays.contains(where: { $0.id == target }) {
            target = displays.first(where: { $0.name.localizedCaseInsensitiveContains("OpenDisplay") || $0.name.localizedCaseInsensitiveContains("Switch") })?.id ?? 0
        }
        if connected && !trusted { disconnect("Accessibility permission was removed.") }
    }
    func grantAccess() {
        let options = [kAXTrustedCheckOptionPrompt.takeUnretainedValue(): true] as CFDictionary
        trusted = AXIsProcessTrustedWithOptions(options)
        status = trusted ? "Accessibility access is enabled." : "Enable OpenDisplay NX Input in System Settings → Privacy & Security → Accessibility."
    }
    func connect() {
        guard !connecting && !connected else { return }
        guard AXIsProcessTrusted() else { status = "Enable Accessibility access before connecting."; return }
        var ipv4 = in_addr()
        guard inet_pton(AF_INET, address.trimmingCharacters(in: .whitespacesAndNewlines), &ipv4) == 1 else {
            status = "Enter the Switch IPv4 address shown in its menu."; return
        }
        guard code.count == 6, code.allSatisfy({ $0 >= "0" && $0 <= "9" }) else {
            status = "Enter the six-digit pairing code shown in the Switch menu."; return
        }
        guard target != 0, displays.contains(where: { $0.id == target }) else {
            status = "Choose the screen shown on your Switch."; return
        }
        disconnect("")
        injector.display = target
        framer = Framer()
        connecting = true
        status = "Connecting…"
        UserDefaults.standard.set(address, forKey: "SwitchAddress")
        let peer = NWConnection(host: NWEndpoint.Host(address.trimmingCharacters(in: .whitespacesAndNewlines)), port: 9011, using: .tcp)
        connection = peer
        lastReply = Date()
        peer.stateUpdateHandler = { [weak self, weak peer] state in
            Task { @MainActor in
            guard let self, let peer, self.connection === peer else { return }
            switch state {
            case .ready:
                self.send(["type": "auth", "code": self.code], to: peer)
                self.receive(from: peer)
            case .failed(let error): self.disconnect("Connection failed: \(error.localizedDescription)")
            case .waiting: self.disconnect("Switch is unavailable. Check its address and Wi-Fi connection.")
            default: break
            }
            }
        }
        peer.start(queue: .main)
        heartbeat = Timer.scheduledTimer(withTimeInterval: 2, repeats: true) { [weak self, weak peer] _ in
            Task { @MainActor in
                guard let self, let peer, self.connection === peer else { return }
                if Date().timeIntervalSince(self.lastReply) > 10 { self.disconnect("Connection timed out."); return }
                if self.connected { self.send(["type": "ping"], to: peer) }
            }
        }
    }
    private func send(_ object: [String: String], to peer: NWConnection) {
        guard let bytes = try? Framer.encode(object) else { disconnect("Invalid outgoing message."); return }
        peer.send(content: bytes, completion: .contentProcessed { [weak self, weak peer] error in
            Task { @MainActor in
            guard let self, let peer, self.connection === peer else { return }
            if let error { self.disconnect("Connection failed: \(error.localizedDescription)") }
            }
        })
    }
    private func receive(from peer: NWConnection) {
        peer.receive(minimumIncompleteLength: 1, maximumLength: 8192) { [weak self, weak peer] bytes, _, ended, error in
            Task { @MainActor in
            guard let self, let peer, self.connection === peer else { return }
            do {
                if let bytes {
                    for payload in try self.framer.feed(bytes) {
                        let message = try Message(data: payload)
                        if !self.connected {
                            guard message.type == "ready" else { throw WireError.message }
                            self.connected = true
                            self.connecting = false
                            self.status = "Connected to \(message.device!). X opens the Switch keyboard."
                            self.code = ""
                        } else if message.type != "ready" {
                            self.injector.handle(message)
                        } else { throw WireError.message }
                        self.lastReply = Date()
                    }
                }
            } catch { self.disconnect("Invalid input data; disconnected."); return }
            if ended || error != nil {
                self.disconnect(self.connected ? "Switch disconnected." : "Pairing failed. Check the code in the Switch menu.")
            } else { self.receive(from: peer) }
            }
        }
    }
    func disconnect(_ message: String = "Disconnected.") {
        connection?.stateUpdateHandler = nil
        connection?.cancel()
        connection = nil
        heartbeat?.invalidate()
        heartbeat = nil
        injector.release()
        connected = false
        connecting = false
        framer = Framer()
        status = message
    }
}
struct CompanionView: View {
    @ObservedObject var model: Companion
    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            HStack(spacing: 14) {
                if let image = NSImage(contentsOfFile: Bundle.main.resourcePath! + "/Icon.png") {
                    Image(nsImage: image).resizable().frame(width: 52, height: 52)
                }
                VStack(alignment: .leading, spacing: 4) {
                    Text("OpenDisplay NX Input").font(.title2.weight(.semibold))
                    Text("Controller and keyboard companion").foregroundStyle(.secondary)
                }
            }
            Text("Keep using the official OpenDisplay Sender. This app adds controller mouse buttons and text input.")
                .foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            Form {
                TextField("Switch address", text: $model.address).disabled(model.connected || model.connecting)
                TextField("Pairing code", text: $model.code).disabled(model.connected || model.connecting)
                Picker("Target screen", selection: $model.target) {
                    Text("Choose a screen").tag(CGDirectDisplayID(0))
                    ForEach(model.displays) { Text($0.name).tag($0.id) }
                }.disabled(model.connected || model.connecting)
            }
            Text("Press + on the connected Switch to see its address and pairing code. Select the same screen that OpenDisplay is sending.")
                .font(.callout).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            HStack {
                if !model.trusted { Button("Enable Accessibility", action: model.grantAccess) }
                Spacer()
                if model.connected || model.connecting {
                    Button("Disconnect") { model.disconnect() }
                } else {
                    Button("Connect", action: model.connect).keyboardShortcut(.defaultAction).disabled(!model.trusted)
                }
            }
            Divider()
            Text(model.status).font(.callout).textSelection(.enabled).fixedSize(horizontal: false, vertical: true)
            Text("L stick: pointer · ZR: left click · ZL: right click\nR stick: scroll · X: keyboard")
                .font(.callout).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
        }.padding(24).frame(width: 430)
    }
}
final class AppDelegate: NSObject, NSApplicationDelegate {
    var onQuit: (() -> Void)?
    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.regular)
        if let path = Bundle.main.resourcePath, let image = NSImage(contentsOfFile: path + "/Icon.png") { NSApp.applicationIconImage = image }
        NSApp.activate(ignoringOtherApps: true)
    }
    func applicationWillTerminate(_ notification: Notification) { onQuit?() }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
}
@main struct NXInputApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var delegate
    @StateObject private var model = Companion()
    var body: some Scene {
        WindowGroup {
            CompanionView(model: model).onAppear { delegate.onQuit = { model.disconnect() } }
        }.windowResizability(.contentSize)
        .commands { CommandGroup(replacing: .newItem) {} }
    }
}
