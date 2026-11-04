import Alumbra
import Foundation

private final class NotificationReference: @unchecked Sendable {
    private let lock = NSLock()
    private weak var player: AlumbraPlayer?

    func attach(_ player: AlumbraPlayer) {
        lock.lock()
        self.player = player
        lock.unlock()
    }

    func scheduleDrain() {
        lock.lock()
        let player = self.player
        lock.unlock()
        Task { @MainActor [weak player] in
            player?.drainEvents()
        }
    }
}

public enum AlumbraError: Error, Sendable, Equatable {
    case invalidArgument
    case invalidHandle
    case unsupported
    case bufferTooSmall
    case wrongThread
    case unknown(Int32)
}

public struct AlumbraMediaError: Error, Sendable, Equatable {
    public let code: UInt32
    public let message: String

    public init(code: UInt32, message: String) {
        self.code = code
        self.message = message
    }
}

public enum AlumbraEvent: Sendable, Equatable {
    case loadStart(sequence: UInt64, currentTime: Double, duration: Double)
    case progress(sequence: UInt64, currentTime: Double, duration: Double)
    case suspend(sequence: UInt64, currentTime: Double, duration: Double)
    case abort(sequence: UInt64, currentTime: Double, duration: Double)
    case error(sequence: UInt64, currentTime: Double, duration: Double)
    case emptied(sequence: UInt64, currentTime: Double, duration: Double)
    case stalled(sequence: UInt64, currentTime: Double, duration: Double)
    case loadedMetadata(sequence: UInt64, currentTime: Double, duration: Double)
    case loadedData(sequence: UInt64, currentTime: Double, duration: Double)
    case canPlay(sequence: UInt64, currentTime: Double, duration: Double)
    case canPlayThrough(sequence: UInt64, currentTime: Double, duration: Double)
    case playing(sequence: UInt64, currentTime: Double, duration: Double)
    case waiting(sequence: UInt64, currentTime: Double, duration: Double)
    case seeking(sequence: UInt64, currentTime: Double, duration: Double)
    case seeked(sequence: UInt64, currentTime: Double, duration: Double)
    case ended(sequence: UInt64, currentTime: Double, duration: Double)
    case durationChange(sequence: UInt64, currentTime: Double, duration: Double)
    case timeUpdate(sequence: UInt64, currentTime: Double, duration: Double)
    case play(sequence: UInt64, currentTime: Double, duration: Double)
    case pause(sequence: UInt64, currentTime: Double, duration: Double)
    case rateChange(sequence: UInt64, currentTime: Double, duration: Double)
    case resize(sequence: UInt64, currentTime: Double, duration: Double)
    case volumeChange(sequence: UInt64, currentTime: Double, duration: Double)
    case warning(sequence: UInt64, currentTime: Double, duration: Double)
}

@MainActor
public final class AlumbraPlayer {
    private var handle: alumbra_handle = 0
    private let notification = NotificationReference()
    private var eventContinuation: AsyncStream<AlumbraEvent>.Continuation?

    public var events: AsyncStream<AlumbraEvent> {
        AsyncStream { continuation in
            eventContinuation = continuation
            continuation.onTermination = { @Sendable [weak self] _ in
                Task { @MainActor [weak self] in
                    self?.eventContinuation = nil
                }
            }
        }
    }

    public init() throws {
        var config = alumbra_player_config()
        config.struct_size = UInt32(MemoryLayout<alumbra_player_config>.size)
        config.notify = alumbraNotify
        config.user_data = Unmanaged.passUnretained(notification).toOpaque()
        try Self.check(alumbra_player_create(&config, &handle))
        notification.attach(self)
    }

    deinit {
        if handle != 0 {
            _ = alumbra_player_destroy(handle)
        }
    }

    public var source: String {
        get throws {
            var size = 0
            let first = alumbra_player_get_source(handle, nil, &size)
            if first != ALUMBRA_STATUS_OK && first != ALUMBRA_STATUS_BUFFER_TOO_SMALL {
                try Self.check(first)
            }
            var bytes = [CChar](repeating: 0, count: size)
            try Self.check(alumbra_player_get_source(handle, &bytes, &size))
            return String(decoding: bytes.map { UInt8(bitPattern: $0) }, as: UTF8.self)
        }
    }

    public func setSource(_ source: String) throws {
        let bytes = Array(source.utf8)
        try bytes.withUnsafeBufferPointer { buffer in
            try Self.check(alumbra_player_set_source(handle, buffer.baseAddress, buffer.count))
        }
    }

    public func load() throws {
        try Self.check(alumbra_player_load(handle))
    }

    public func play() throws {
        try Self.check(alumbra_player_play(handle))
    }

    public func pause() throws {
        try Self.check(alumbra_player_pause(handle))
    }

    public var currentTime: Double {
        get throws {
            var value = 0.0
            try Self.check(alumbra_player_get_current_time(handle, &value))
            return value
        }
    }

    public func setCurrentTime(_ value: Double) throws {
        try Self.check(alumbra_player_set_current_time(handle, value))
    }

    public var isSeeking: Bool {
        get throws {
            var value: UInt8 = 0
            try Self.check(alumbra_player_get_seeking(handle, &value))
            return value != 0
        }
    }

    public var volume: Double {
        get throws {
            var value = 0.0
            try Self.check(alumbra_player_get_volume(handle, &value))
            return value
        }
    }

    public func setVolume(_ value: Double) throws {
        try Self.check(alumbra_player_set_volume(handle, value))
    }

    public var isMuted: Bool {
        get throws {
            var value: UInt8 = 0
            try Self.check(alumbra_player_get_muted(handle, &value))
            return value != 0
        }
    }

    public func setMuted(_ value: Bool) throws {
        try Self.check(alumbra_player_set_muted(handle, value ? 1 : 0))
    }

    public var playbackRate: Double {
        get throws {
            var value = 0.0
            try Self.check(alumbra_player_get_playback_rate(handle, &value))
            return value
        }
    }

    public func setPlaybackRate(_ value: Double) throws {
        try Self.check(alumbra_player_set_playback_rate(handle, value))
    }

    public var readyState: UInt32 {
        get throws {
            var value: UInt32 = 0
            try Self.check(alumbra_player_get_ready_state(handle, &value))
            return value
        }
    }

    public var networkState: UInt32 {
        get throws {
            var value: UInt32 = 0
            try Self.check(alumbra_player_get_network_state(handle, &value))
            return value
        }
    }

    public func setting(for key: String) throws -> String {
        let keyBytes = Array(key.utf8)
        return try keyBytes.withUnsafeBufferPointer { keyBuffer in
            var size = 0
            let first = alumbra_settings_get(handle, keyBuffer.baseAddress, keyBuffer.count, nil, &size)
            if first != ALUMBRA_STATUS_OK && first != ALUMBRA_STATUS_BUFFER_TOO_SMALL {
                try Self.check(first)
            }
            var bytes = [CChar](repeating: 0, count: size)
            try Self.check(alumbra_settings_get(handle, keyBuffer.baseAddress, keyBuffer.count, &bytes, &size))
            return String(decoding: bytes.map { UInt8(bitPattern: $0) }, as: UTF8.self)
        }
    }

    public func setSetting(_ value: String, for key: String) throws {
        let keyBytes = Array(key.utf8)
        let valueBytes = Array(value.utf8)
        try keyBytes.withUnsafeBufferPointer { keyBuffer in
            try valueBytes.withUnsafeBufferPointer { valueBuffer in
                try Self.check(alumbra_settings_set(handle, keyBuffer.baseAddress, keyBuffer.count, valueBuffer.baseAddress, valueBuffer.count))
            }
        }
    }

    public func mediaError() throws -> AlumbraMediaError? {
        var info = alumbra_error_info()
        info.struct_size = UInt32(MemoryLayout<alumbra_error_info>.size)
        var size = 0
        let first = alumbra_player_get_error(handle, &info, nil, &size)
        if first != ALUMBRA_STATUS_OK && first != ALUMBRA_STATUS_BUFFER_TOO_SMALL {
            try Self.check(first)
        }
        if info.code == 0 {
            return nil
        }
        var bytes = [CChar](repeating: 0, count: size)
        try Self.check(alumbra_player_get_error(handle, &info, &bytes, &size))
        return AlumbraMediaError(code: info.code, message: String(decoding: bytes.map { UInt8(bitPattern: $0) }, as: UTF8.self))
    }

    public func drainEvents() {
        var event = alumbra_event()
        event.struct_size = UInt32(MemoryLayout<alumbra_event>.size)
        while true {
            var available: UInt8 = 0
            guard alumbra_player_poll_event(handle, &event, &available) == ALUMBRA_STATUS_OK,
                  available != 0 else {
                return
            }
            if let mapped = Self.map(event) {
                eventContinuation?.yield(mapped)
            }
        }
    }

    private static func check(_ status: alumbra_status) throws {
        switch status {
        case ALUMBRA_STATUS_OK:
            return
        case ALUMBRA_STATUS_INVALID_ARGUMENT:
            throw AlumbraError.invalidArgument
        case ALUMBRA_STATUS_INVALID_HANDLE:
            throw AlumbraError.invalidHandle
        case ALUMBRA_STATUS_UNSUPPORTED:
            throw AlumbraError.unsupported
        case ALUMBRA_STATUS_BUFFER_TOO_SMALL:
            throw AlumbraError.bufferTooSmall
        case ALUMBRA_STATUS_WRONG_THREAD:
            throw AlumbraError.wrongThread
        default:
            throw AlumbraError.unknown(status)
        }
    }

    private static func map(_ event: alumbra_event) -> AlumbraEvent? {
        switch Int32(event.type) {
        case ALUMBRA_EVENT_LOADSTART: .loadStart(sequence: event.sequence, currentTime: event.current_time, duration: event.duration)
        case ALUMBRA_EVENT_PROGRESS: .progress(sequence: event.sequence, currentTime: event.current_time, duration: event.duration)
        case ALUMBRA_EVENT_SUSPEND: .suspend(sequence: event.sequence, currentTime: event.current_time, duration: event.duration)
        case ALUMBRA_EVENT_ABORT: .abort(sequence: event.sequence, currentTime: event.current_time, duration: event.duration)
        case ALUMBRA_EVENT_ERROR: .error(sequence: event.sequence, currentTime: event.current_time, duration: event.duration)
        case ALUMBRA_EVENT_EMPTIED: .emptied(sequence: event.sequence, currentTime: event.current_time, duration: event.duration)
        case ALUMBRA_EVENT_STALLED: .stalled(sequence: event.sequence, currentTime: event.current_time, duration: event.duration)
        case ALUMBRA_EVENT_LOADEDMETADATA: .loadedMetadata(sequence: event.sequence, currentTime: event.current_time, duration: event.duration)
        case ALUMBRA_EVENT_LOADEDDATA: .loadedData(sequence: event.sequence, currentTime: event.current_time, duration: event.duration)
        case ALUMBRA_EVENT_CANPLAY: .canPlay(sequence: event.sequence, currentTime: event.current_time, duration: event.duration)
        case ALUMBRA_EVENT_CANPLAYTHROUGH: .canPlayThrough(sequence: event.sequence, currentTime: event.current_time, duration: event.duration)
        case ALUMBRA_EVENT_PLAYING: .playing(sequence: event.sequence, currentTime: event.current_time, duration: event.duration)
        case ALUMBRA_EVENT_WAITING: .waiting(sequence: event.sequence, currentTime: event.current_time, duration: event.duration)
        case ALUMBRA_EVENT_SEEKING: .seeking(sequence: event.sequence, currentTime: event.current_time, duration: event.duration)
        case ALUMBRA_EVENT_SEEKED: .seeked(sequence: event.sequence, currentTime: event.current_time, duration: event.duration)
        case ALUMBRA_EVENT_ENDED: .ended(sequence: event.sequence, currentTime: event.current_time, duration: event.duration)
        case ALUMBRA_EVENT_DURATIONCHANGE: .durationChange(sequence: event.sequence, currentTime: event.current_time, duration: event.duration)
        case ALUMBRA_EVENT_TIMEUPDATE: .timeUpdate(sequence: event.sequence, currentTime: event.current_time, duration: event.duration)
        case ALUMBRA_EVENT_PLAY: .play(sequence: event.sequence, currentTime: event.current_time, duration: event.duration)
        case ALUMBRA_EVENT_PAUSE: .pause(sequence: event.sequence, currentTime: event.current_time, duration: event.duration)
        case ALUMBRA_EVENT_RATECHANGE: .rateChange(sequence: event.sequence, currentTime: event.current_time, duration: event.duration)
        case ALUMBRA_EVENT_RESIZE: .resize(sequence: event.sequence, currentTime: event.current_time, duration: event.duration)
        case ALUMBRA_EVENT_VOLUMECHANGE: .volumeChange(sequence: event.sequence, currentTime: event.current_time, duration: event.duration)
        case ALUMBRA_EVENT_WARNING: .warning(sequence: event.sequence, currentTime: event.current_time, duration: event.duration)
        default: nil
        }
    }
}

private func alumbraNotify(_ userData: UnsafeMutableRawPointer?) {
    guard let userData else { return }
    Unmanaged<NotificationReference>.fromOpaque(userData).takeUnretainedValue().scheduleDrain()
}
