// ToolRunner.swift -- the only way this app touches the mouse.
//
// IT SHELLS OUT. Every device operation runs `egg-config` or `egg-flash` as a
// subprocess. That is not laziness about linking; it is the design.
//
// CLAUDE.md §3: "A GUI DOES NOT INHERIT THIS. If one is built, the
// contradictory quit semantics in §4 are the whole reason the two executables
// are separate: a window has a close button, a Dock quit item and a
// force-quit, none of which a post-erase write phase may honour. A GUI may
// drive config freely; it must not own the flash write phase. Shell out to
// `egg-flash` and let it keep being a process that cannot be asked to stop."
//
// The second reason is §4.3. The write paths are small, in one file each,
// mutation-tested, and scored against Endgame's own captured bytes. A GUI that
// linked EGGConfigCore would become a SECOND caller of those paths, and every
// guard -- read-modify-write, the one-byte self-check, diff-verify, the refusal
// list, the --confirm token -- would need re-checking against a caller nobody
// had tested. Driving the CLI means this app can be wrong about which command
// to run and still cannot be wrong about what the command does.
//
// So the app's whole correctness obligation is: build the right argument list,
// show the output honestly, and NEVER kill a running flash.
import Foundation

/// One completed run of a CLI.
struct ToolResult {
    let status: Int32
    let stdout: String
    let stderr: String
    var ok: Bool { status == 0 }
    var text: String { stdout + (stderr.isEmpty ? "" : "\n" + stderr) }
}

enum ToolError: LocalizedError {
    case notFound(String)
    case launchFailed(String, String)

    var errorDescription: String? {
        switch self {
        case .notFound(let name):
            return """
            \(name) was not found.

            This app drives the command-line tools rather than reimplementing \
            them, so it needs them next to it. Build them with:

                cmake -S . -B build && cmake --build build

            and launch this app from the same repository.
            """
        case .launchFailed(let name, let why):
            return "\(name) could not be started: \(why)"
        }
    }
}

/// Runs `egg-config` and `egg-flash`, and enforces the one rule a GUI can break
/// that a CLI cannot.
@MainActor
final class ToolRunner: ObservableObject {

    /// True from the moment a flash write phase begins until the process exits.
    ///
    /// §4.2: "After erase: quitting is the bug. The device has no valid
    /// application; exiting cleanly guarantees the bad outcome." The CLI already
    /// ignores SIGINT/SIGTERM/SIGHUP for that window (NoQuitDuringWrite). What a
    /// GUI adds is a window close button, a Dock quit item, and a user who
    /// thinks closing a window is free. This flag is what the app checks before
    /// it lets any of those do anything.
    @Published private(set) var writePhaseInProgress = false

    /// Live output of the running command, appended as it arrives.
    @Published private(set) var liveOutput = ""

    @Published private(set) var running: String?

    /// Where the executables are. Searched next to the app, then in ./build,
    /// then on PATH -- so a developer running from the repo and a user running
    /// a copied bundle both work without configuration.
    static func locate(_ name: String) -> URL? {
        var candidates: [URL] = []
        let bundleDir = Bundle.main.bundleURL.deletingLastPathComponent()
        candidates.append(bundleDir.appendingPathComponent(name))
        candidates.append(bundleDir.appendingPathComponent("build/\(name)"))
        // Bundle.main.bundleURL for "Foo.app" is .../Foo.app; go up to the repo
        // when the app sits in build/.
        candidates.append(bundleDir.deletingLastPathComponent()
                                   .appendingPathComponent("build/\(name)"))
        candidates.append(URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
                            .appendingPathComponent("build/\(name)"))
        for p in (ProcessInfo.processInfo.environment["PATH"] ?? "").split(separator: ":") {
            candidates.append(URL(fileURLWithPath: String(p)).appendingPathComponent(name))
        }
        return candidates.first { FileManager.default.isExecutableFile(atPath: $0.path) }
    }

    /// Run a tool to completion, capturing output. `isWritePhase` marks the run
    /// as unkillable; see `writePhaseInProgress`.
    func run(_ tool: String,
             _ args: [String],
             workingDirectory: URL? = nil,
             isWritePhase: Bool = false) async throws -> ToolResult {
        guard let exe = Self.locate(tool) else { throw ToolError.notFound(tool) }

        running = ([tool] + args).joined(separator: " ")
        liveOutput = ""
        if isWritePhase { writePhaseInProgress = true }
        defer {
            running = nil
            if isWritePhase { writePhaseInProgress = false }
        }

        let p = Process()
        p.executableURL = exe
        p.arguments = args
        if let wd = workingDirectory { p.currentDirectoryURL = wd }

        let outPipe = Pipe(), errPipe = Pipe()
        p.standardOutput = outPipe
        p.standardError = errPipe

        // Stream stdout so a 65-block flash shows progress rather than a beach
        // ball. The handler runs off the main actor, so the hop is explicit.
        let collected = OutputCollector()
        outPipe.fileHandleForReading.readabilityHandler = { h in
            let d = h.availableData
            guard !d.isEmpty, let s = String(data: d, encoding: .utf8) else { return }
            Task { @MainActor [weak self] in
                await collected.append(s)
                self?.liveOutput += s
            }
        }

        do {
            try p.run()
        } catch {
            outPipe.fileHandleForReading.readabilityHandler = nil
            throw ToolError.launchFailed(tool, error.localizedDescription)
        }

        // NOTE: there is deliberately no timeout and no cancellation path here.
        // A flash that is taking a long time is not a flash to abandon.
        await withCheckedContinuation { (c: CheckedContinuation<Void, Never>) in
            p.terminationHandler = { _ in c.resume() }
        }
        outPipe.fileHandleForReading.readabilityHandler = nil

        let err = String(data: errPipe.fileHandleForReading.readDataToEndOfFile(),
                         encoding: .utf8) ?? ""
        return ToolResult(status: p.terminationStatus,
                          stdout: await collected.text,
                          stderr: err)
    }

    /// Run a helper script through python3. Used only for the update pipeline,
    /// which reads .exe files and touches no hardware.
    func runPython(_ script: String, _ args: [String],
                   repoRoot: URL) async throws -> ToolResult {
        let p = Process()
        p.executableURL = URL(fileURLWithPath: "/usr/bin/env")
        p.arguments = ["python3", script] + args
        p.currentDirectoryURL = repoRoot
        let outPipe = Pipe(), errPipe = Pipe()
        p.standardOutput = outPipe
        p.standardError = errPipe
        running = "python3 \(script)"
        defer { running = nil }
        do { try p.run() } catch {
            throw ToolError.launchFailed("python3", error.localizedDescription)
        }
        let out = String(data: outPipe.fileHandleForReading.readDataToEndOfFile(),
                         encoding: .utf8) ?? ""
        let err = String(data: errPipe.fileHandleForReading.readDataToEndOfFile(),
                         encoding: .utf8) ?? ""
        p.waitUntilExit()
        return ToolResult(status: p.terminationStatus, stdout: out, stderr: err)
    }
}

/// Accumulates streamed output off the main actor.
actor OutputCollector {
    private var buf = ""
    func append(_ s: String) { buf += s }
    var text: String { buf }
}
