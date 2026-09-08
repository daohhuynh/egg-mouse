// ToolRunner.swift -- the only way this app touches the mouse.
//
// IT SHELLS OUT. Every device operation runs `egg-config` or `egg-flash` as a
// subprocess. That is not laziness about linking; it is the design.
//
// engineering-rules.md §3: "A GUI DOES NOT INHERIT THIS. If one is built, the
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
    /// ignores SIGINT/SIGTERM/SIGHUP/SIGPIPE for that window
    /// (NoQuitDuringWrite), in the CHILD process, which outlives this app.
    ///
    /// So this flag does NOT gate the window close button or the Dock quit item
    /// -- it cannot, and it does not need to (see EGGApp.swift's header). It
    /// gates what this app can actually get wrong: starting a second command
    /// while one is in flight, and navigating away from the screen showing it.
    /// Every one of its readers is a banner, a title, or a `.disabled`.
    ///
    /// CORRECTED 2026-09-07. This comment used to end "This flag is what the app
    /// checks before it lets any of those do anything", naming a window-close
    /// guard that has never existed in this target.
    @Published private(set) var writePhaseInProgress = false

    /// Live output of the running command, appended as it arrives.
    @Published private(set) var liveOutput = ""

    @Published private(set) var running: String?

    /// Hex-dump every frame. Off by default, and a user-visible switch on the
    /// Advanced screen.
    ///
    /// WHY IT IS SAFE TO APPLY GLOBALLY, which is not obvious and is the whole
    /// reason this lives here rather than in Commands. `-v` is a LOGGING flag
    /// in both CLIs: each parses it into a `Log` and nothing else branches on
    /// it, so the byte stream a verb produces is identical with and without it.
    /// Tests/test_config_set.sh proves that by diffing `dryrun`'s emitted frame
    /// both ways -- an assertion about the frames, not about the parser.
    ///
    /// Every OTHER argument is built by Commands and driven by
    /// Tests/test_app_commands.swift against the real tools. This one is the
    /// single exception and it is the only kind that could be: a flag that
    /// cannot change what is sent.
    @Published var verbose = false

    /// Where the executables are. Searched INSIDE the bundle first, then next
    /// to the app, then in ./build, then on PATH -- so a downloaded release, a
    /// Homebrew install and a developer running from the repo all work with no
    /// configuration.
    static func locate(_ name: String) -> URL? {
        var candidates: [URL] = []
        // Contents/MacOS/, i.e. beside this app's own executable. FIRST on
        // purpose: a release .dmg ships egg-config and egg-flash inside the
        // bundle so the download is one self-contained thing, and a bundle
        // that carries its own tools must never prefer a stray copy it found
        // in some working directory. Bundle.main.bundleURL is ".../Foo.app",
        // which is why this cannot be derived from it by appending a name --
        // the old first candidate resolved to a SIBLING of the .app, and so
        // nothing inside the bundle was ever searched at all.
        if let inBundle = Bundle.main.executableURL?
                            .deletingLastPathComponent()
                            .appendingPathComponent(name) {
            candidates.append(inBundle)
        }
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

        // See `verbose`. Prepended, because both tools' parsers take flags
        // anywhere but their verb dispatch reads args[0].
        //
        // Through `Commands.verbose` rather than inline `["-v"] + args`: that
        // function existed, carried the whole argument for why the flag is safe
        // to apply globally, and was called from nowhere -- so the reasoning
        // lived next to dead code while the live code did the same thing a
        // second time (2026-09-07). Tests/test_gui_state.py now fails on a
        // Commands builder no view or runner calls.
        let args = verbose ? Commands.verbose(args) : args
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

        do {
            try p.run()
        } catch {
            throw ToolError.launchFailed(tool, error.localizedDescription)
        }

        // BOTH pipes are drained to EOF, on their own threads, BEFORE the exit
        // status is collected. Three separate defects live in the order of
        // those three things, and this function has had all three.
        //
        //  1. TRUNCATION, which is what sent us here (2026-09-08). The previous
        //     version streamed stdout through a `readabilityHandler` that
        //     hopped to `Task { @MainActor }` to append, then read the buffer
        //     the instant `terminationHandler` fired. Nothing sequenced the
        //     hops against that read, so a chunk still queued on the main actor
        //     was simply missing from the result. It never showed up in a
        //     harness, because a test binary's main actor is idle and the hops
        //     always won the race; in a launching app, which is exactly when
        //     ConfigView's `.task` runs, it is not idle. Under artificial main
        //     actor load the old code returned ZERO bytes of a 5,016-byte
        //     listing about once in forty runs, and `egg-config set` returning
        //     nothing is a Settings screen with an empty field menu.
        //
        //     Correctness no longer depends on the main actor at all: the
        //     reader appends bytes synchronously on its own thread, and the
        //     hop to the main actor is now display-only. It publishes the whole
        //     buffer rather than its own chunk, so an out-of-order hop can show
        //     stale text for an instant but cannot show WRONG text.
        //
        //  2. DEADLOCK on a big log. Waiting for exit before reading a pipe
        //     hangs as soon as a tool writes more than the pipe buffer holds
        //     (64 KiB), because the child blocks in write() and never exits.
        //     `egg-flash` writing 65 blocks of progress is the case that would
        //     have found it. Read first, wait after.
        //
        //  3. A DROPPED CHUNK on a split character. The old handler did
        //     `guard let s = String(data: d, encoding: .utf8) else { return }`,
        //     so a chunk boundary landing inside a multi-byte character threw
        //     that chunk away. Bytes are accumulated as Data and decoded once,
        //     at the end, where no boundary can fall inside a character.
        //
        // NOTE: there is deliberately no timeout and no cancellation path here.
        // A flash that is taking a long time is not a flash to abandon.
        let outBuf = OutputBuffer(), errBuf = OutputBuffer()
        let outHandle = outPipe.fileHandleForReading
        let errHandle = errPipe.fileHandleForReading
        await withCheckedContinuation { (c: CheckedContinuation<Void, Never>) in
            let group = DispatchGroup()
            let q = DispatchQueue.global(qos: .userInitiated)
            q.async(group: group) {
                // Stream stdout so a 65-block flash shows progress rather than
                // a beach ball. `availableData` blocks until there is data or
                // the writer closes, and returns empty exactly at EOF.
                while true {
                    let d = outHandle.availableData
                    if d.isEmpty { break }
                    outBuf.append(d)
                    Task { @MainActor [weak self] in self?.liveOutput = outBuf.text }
                }
            }
            q.async(group: group) {
                while true {
                    let d = errHandle.availableData
                    if d.isEmpty { break }
                    errBuf.append(d)
                }
            }
            group.notify(queue: q) {
                p.waitUntilExit()
                c.resume()
            }
        }

        return ToolResult(status: p.terminationStatus,
                          stdout: outBuf.text,
                          stderr: errBuf.text)
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

/// Accumulates a tool's output off the main actor.
///
/// A lock rather than an actor, deliberately. An actor can only be appended to
/// from an async context, which is what forced the old code to hop to the main
/// actor to record a chunk and is the whole reason output could go missing
/// (see `run`). Appending has to be synchronous so that it happens on the
/// reader thread, in order, whether or not anything else is scheduled.
final class OutputBuffer: @unchecked Sendable {
    private let lock = NSLock()
    private var buf = Data()

    func append(_ d: Data) {
        lock.lock(); defer { lock.unlock() }
        buf.append(d)
    }

    /// Decoded once, over whole bytes, so no chunk boundary can fall inside a
    /// multi-byte character. Invalid bytes become U+FFFD rather than throwing
    /// the output away: a tool that emits one bad byte still has things to say.
    var text: String {
        lock.lock(); defer { lock.unlock() }
        return String(decoding: buf, as: UTF8.self)
    }
}
