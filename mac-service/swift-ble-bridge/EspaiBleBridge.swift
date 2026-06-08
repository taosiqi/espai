import CoreBluetooth
import Foundation

final class Logger {
    private let handle: FileHandle?

    init() {
        let dir = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent(".codex", isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let file = dir.appendingPathComponent("espai-ble-bridge-app.log")
        if !FileManager.default.fileExists(atPath: file.path) {
            FileManager.default.createFile(atPath: file.path, contents: nil)
        }
        handle = try? FileHandle(forWritingTo: file)
        _ = try? handle?.seekToEnd()
    }

    func write(_ message: String) {
        let stamp = ISO8601DateFormatter().string(from: Date())
        let line = "[\(stamp)] \(message)\n"
        if let data = line.data(using: .utf8) {
            try? handle?.write(contentsOf: data)
        }
    }
}

enum QuotaError: Error, LocalizedError {
    case invalidResponse
    case missingBucket
    case missingPrimaryWindow
    case noSessionSnapshot
    case rpc(String)
    case timeout(String)

    var errorDescription: String? {
        switch self {
        case .invalidResponse: return "Invalid Codex quota response"
        case .missingBucket: return "No Codex rate limit bucket found"
        case .missingPrimaryWindow: return "Codex quota primary window is missing"
        case .noSessionSnapshot: return "No Codex quota snapshot found in ~/.codex/sessions"
        case .rpc(let message): return message
        case .timeout(let method): return "Codex RPC timeout: \(method)"
        }
    }
}

final class QuotaReader {
    private let logger: Logger

    init(logger: Logger) {
        self.logger = logger
    }

    func payloadData() throws -> Data {
        let quota = try readQuotaFromSessionLogs()
        logQuota("session-log", quota)
        var object = quota
        object["now"] = Int(Date().timeIntervalSince1970)
        return try JSONSerialization.data(withJSONObject: object)
    }

    private func logQuota(_ source: String, _ quota: [String: Any]) {
        let primary = quota["primary"] as? [String: Any]
        let secondary = quota["secondary"] as? [String: Any]
        let primaryText = "\(primary?["label"] as? String ?? "?") \(primary?["remainingPercent"] as? Int ?? -1)%"
        let secondaryText = secondary.map { ", \($0["label"] as? String ?? "?") \($0["remainingPercent"] as? Int ?? -1)%" } ?? ""
        logger.write("\(source): \(primaryText)\(secondaryText)")
    }
}

func normalizeQuota(_ result: [String: Any]) throws -> [String: Any] {
    let byLimitId = result["rateLimitsByLimitId"] as? [String: Any]
    let codexBucket = byLimitId?["codex"] as? [String: Any]
    let fallbackBucket = result["rateLimits"] as? [String: Any]
    guard let bucket = codexBucket ?? fallbackBucket else {
        throw QuotaError.missingBucket
    }
    guard let primary = parseWindow(bucket["primary"]) else {
        throw QuotaError.missingPrimaryWindow
    }

    return [
        "ok": true,
        "source": codexBucket != nil ? "codex-app-server" : "codex-app-server-fallback",
        "limitId": bucket["limitId"] ?? NSNull(),
        "limitName": bucket["limitName"] ?? NSNull(),
        "planType": bucket["planType"] ?? NSNull(),
        "primary": primary,
        "secondary": parseWindow(bucket["secondary"]) ?? NSNull(),
        "updatedAt": Int(Date().timeIntervalSince1970),
    ]
}

func readQuotaFromSessionLogs() throws -> [String: Any] {
    let root = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent(".codex/sessions")
    let files = newestJsonlFiles(root: root, limit: 120)
    for file in files {
        if let snapshot = parseSessionFile(file) {
            return snapshot
        }
    }
    throw QuotaError.noSessionSnapshot
}

func newestJsonlFiles(root: URL, limit: Int) -> [URL] {
    guard let enumerator = FileManager.default.enumerator(
        at: root,
        includingPropertiesForKeys: [.contentModificationDateKey, .isRegularFileKey],
        options: [.skipsHiddenFiles]
    ) else {
        return []
    }

    var files: [(URL, Date)] = []
    for case let url as URL in enumerator where url.pathExtension == "jsonl" {
        guard let values = try? url.resourceValues(forKeys: [.contentModificationDateKey, .isRegularFileKey]),
              values.isRegularFile == true else {
            continue
        }
        files.append((url, values.contentModificationDate ?? .distantPast))
    }
    return files.sorted { $0.1 > $1.1 }.prefix(limit).map(\.0)
}

func parseSessionFile(_ url: URL) -> [String: Any]? {
    guard let data = try? Data(contentsOf: url) else { return nil }
    let tail = data.count > 4 * 1024 * 1024 ? data.suffix(4 * 1024 * 1024) : data[...]
    guard let text = String(data: Data(tail), encoding: .utf8) else { return nil }

    for line in text.split(separator: "\n").reversed() {
        guard let lineData = line.trimmingCharacters(in: .whitespacesAndNewlines).data(using: .utf8),
              let payload = try? JSONSerialization.jsonObject(with: lineData) as? [String: Any],
              payload["type"] as? String == "event_msg",
              let inner = payload["payload"] as? [String: Any],
              inner["type"] as? String == "token_count",
              let rateLimits = inner["rate_limits"] as? [String: Any],
              (rateLimits["limit_id"] as? String)?.lowercased() == "codex",
              let primary = parseSessionWindow(rateLimits["primary"]) else {
            continue
        }

        return [
            "ok": true,
            "source": "codex-session-log",
            "sourceFileName": url.lastPathComponent,
            "limitId": "codex",
            "limitName": rateLimits["limit_name"] ?? NSNull(),
            "planType": rateLimits["plan_type"] ?? NSNull(),
            "primary": primary,
            "secondary": parseSessionWindow(rateLimits["secondary"]) ?? NSNull(),
            "updatedAt": timestampToEpoch(payload["timestamp"]) ?? Int(Date().timeIntervalSince1970),
        ]
    }
    return nil
}

func parseWindow(_ value: Any?) -> [String: Any]? {
    guard let object = value as? [String: Any] else { return nil }
    let usedPercent = clampPercent(toNumber(object["usedPercent"]))
    let minutes = toNumber(object["windowDurationMins"])
    return quotaWindow(usedPercent: usedPercent, minutes: minutes, resetsAt: toNumber(object["resetsAt"]))
}

func parseSessionWindow(_ value: Any?) -> [String: Any]? {
    guard let object = value as? [String: Any] else { return nil }
    let usedPercent = clampPercent(toNumber(object["used_percent"]))
    let minutes = toNumber(object["window_minutes"])
    return quotaWindow(usedPercent: usedPercent, minutes: minutes, resetsAt: toNumber(object["resets_at"]))
}

func quotaWindow(usedPercent: Int, minutes: Double?, resetsAt: Double?) -> [String: Any] {
    [
        "label": windowLabel(minutes),
        "usedPercent": usedPercent,
        "remainingPercent": max(0, 100 - usedPercent),
        "resetsAt": resetsAt.map { Int($0) } ?? NSNull(),
        "windowDurationMins": minutes.map { Int($0) } ?? NSNull(),
    ]
}

func windowLabel(_ minutes: Double?) -> String {
    guard let mins = minutes.map(Int.init) else { return "?" }
    if mins == 300 { return "5h" }
    if mins == 10080 { return "7d" }
    if mins % 1440 == 0 { return "\(mins / 1440)d" }
    if mins % 60 == 0 { return "\(mins / 60)h" }
    return "\(mins)m"
}

func toNumber(_ value: Any?) -> Double? {
    if let number = value as? NSNumber { return number.doubleValue }
    if let string = value as? String { return Double(string.trimmingCharacters(in: .whitespacesAndNewlines)) }
    return nil
}

func clampPercent(_ value: Double?) -> Int {
    min(100, max(0, Int((value ?? 0).rounded())))
}

func timestampToEpoch(_ value: Any?) -> Int? {
    guard let string = value as? String else { return nil }
    let formatter = ISO8601DateFormatter()
    return formatter.date(from: string).map { Int($0.timeIntervalSince1970) }
}

final class EspaiBleBridge: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    private let logger = Logger()
    private lazy var quotaReader = QuotaReader(logger: logger)
    private let serviceUUID = CBUUID(string: "d34d3b80-2e0b-4b7a-9d68-28db61b3d1a0")
    private let quotaRxUUID = CBUUID(string: "d34d3b81-2e0b-4b7a-9d68-28db61b3d1a0")
    private let pollSeconds = TimeInterval(ProcessInfo.processInfo.environment["ESPAI_BLE_POLL_SECONDS"].flatMap(Double.init) ?? 60)
    private let chunkBytes = Int(ProcessInfo.processInfo.environment["ESPAI_BLE_CHUNK_BYTES"].flatMap(Int.init) ?? 180)

    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var quotaCharacteristic: CBCharacteristic?
    private var timer: Timer?

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: .main)
        logger.write("EspaiBleBridge starting")
    }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn:
            logger.write("Bluetooth powered on, scanning for espai-s3")
            central.scanForPeripherals(withServices: [serviceUUID])
        case .poweredOff:
            logger.write("Bluetooth is off")
        case .unauthorized:
            logger.write("Bluetooth permission is not authorized")
        default:
            logger.write("Bluetooth state: \(central.state.rawValue)")
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral, advertisementData: [String: Any], rssi RSSI: NSNumber) {
        logger.write("Discovered \(peripheral.name ?? peripheral.identifier.uuidString), connecting")
        self.peripheral = peripheral
        peripheral.delegate = self
        central.stopScan()
        central.connect(peripheral)
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        logger.write("Connected to \(peripheral.name ?? peripheral.identifier.uuidString)")
        peripheral.discoverServices([serviceUUID])
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        logger.write("Disconnected: \(error?.localizedDescription ?? "normal")")
        quotaCharacteristic = nil
        timer?.invalidate()
        timer = nil
        central.scanForPeripherals(withServices: [serviceUUID])
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if let error {
            logger.write("Discover services failed: \(error.localizedDescription)")
            central.cancelPeripheralConnection(peripheral)
            return
        }
        for service in peripheral.services ?? [] where service.uuid == serviceUUID {
            peripheral.discoverCharacteristics([quotaRxUUID], for: service)
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        if let error {
            logger.write("Discover characteristics failed: \(error.localizedDescription)")
            central.cancelPeripheralConnection(peripheral)
            return
        }
        for characteristic in service.characteristics ?? [] where characteristic.uuid == quotaRxUUID {
            quotaCharacteristic = characteristic
            logger.write("Quota characteristic ready")
            pushQuota()
            timer = Timer.scheduledTimer(withTimeInterval: pollSeconds, repeats: true) { [weak self] _ in
                self?.pushQuota()
            }
        }
    }

    private func pushQuota() {
        guard let peripheral, let characteristic = quotaCharacteristic else { return }

        DispatchQueue.global(qos: .utility).async { [weak self, weak peripheral] in
            guard let self, let peripheral else { return }
            do {
                let payload = try self.quotaReader.payloadData()
                DispatchQueue.main.async {
                    self.write(payload, to: characteristic, on: peripheral)
                    self.logger.write("Wrote \(payload.count) bytes")
                }
            } catch {
                self.logger.write("Push failed: \(error.localizedDescription)")
            }
        }
    }

    private func write(_ payload: Data, to characteristic: CBCharacteristic, on peripheral: CBPeripheral) {
        let writeType: CBCharacteristicWriteType = characteristic.properties.contains(.write) ? .withResponse : .withoutResponse
        peripheral.writeValue(Data("BEGIN:\(payload.count)".utf8), for: characteristic, type: .withResponse)
        var offset = 0
        while offset < payload.count {
            let end = min(offset + chunkBytes, payload.count)
            peripheral.writeValue(payload.subdata(in: offset..<end), for: characteristic, type: writeType)
            offset = end
        }
        peripheral.writeValue(Data("END".utf8), for: characteristic, type: .withResponse)
    }
}

let bridge = EspaiBleBridge()
RunLoop.main.run()
