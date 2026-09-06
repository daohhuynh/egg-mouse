// Commands.swift -- every argument list this app can build, in one place.
//
// The app's whole correctness obligation is "build the right argument list"
// (see ToolRunner.swift). Leaving that obligation scattered through view code
// would make it exactly as testable as a button's tooltip, so it lives here as
// pure functions over plain values and Tests/test_app_commands.swift drives
// them against the real CLIs.
//
// The rules encoded here, each traceable to CLAUDE.md:
//   §4.1  a write is previewed before it is applied; --yes is never implicit
//   §4.2  a flash requires a backup file that already exists
//   §4.2c a flash carries a --confirm token the person typed, never one this
//         app lifted out of a dry run for them
//   §5    a firmware release that has not been proven on this device needs its
//         own separate acknowledgement, not --yes
import Foundation

enum Commands {

    // ------------------------------------------------------------- egg-config
    static func readSettings(savingTo path: String? = nil) -> [String] {
        var a = ["read"]
        if let p = path, !p.isEmpty { a += ["--save", p] }
        return a
    }

    static func deviceInfo() -> [String] { ["info"] }

    static func factoryReset() -> [String] { ["factory-reset", "--yes"] }

    /// RESTORE, which is the undo `factoryReset` needs to exist beside it.
    ///
    /// The Settings screen has been able to wipe the mouse and to save a copy
    /// since the day it shipped, and its own tooltip said "Your saved copy is
    /// the way back" -- while offering no way back. CLAUDE.md 4.1 puts it the
    /// other way round: implement the undo FIRST, then the thing it undoes.
    ///
    /// The preview is genuinely free. `egg-config dryrun REC` with no field
    /// prints the exact 1041-byte A0 11 frame and its diff against the saved
    /// record WITHOUT opening the device at all, so a restore can be inspected
    /// in full with the mouse unplugged.
    static func previewRestore(record path: String) -> [String] {
        ["dryrun", path]
    }

    static func applyRestore(record path: String) -> [String] {
        ["restore", path, "--yes"]
    }

    /// Where egg-config keeps the automatic known-good copy. Hardcoded here
    /// to match `defaultVaultPath()` in egg-config/main.cpp, and checked
    /// against the CLI's own help text by Tests/test_app_commands.swift --
    /// a dotfile in $HOME is not something a person can find in an open
    /// panel, so the GUI has to know the name to offer it at all.
    static let knownGoodVaultName = ".egg-mouse-known-good.bin"

    static func knownGoodVaultPath() -> String {
        (NSHomeDirectory() as NSString)
            .appendingPathComponent(knownGoodVaultName)
    }

    /// Preview a field change. Sends nothing: `set` without --yes is a dry run
    /// that prints the byte it would move.
    static func previewSet(field: String, value: String) -> [String] {
        ["set", field, value]
    }

    static func applySet(field: String, value: String) -> [String] {
        ["set", field, value, "--yes"]
    }

    static func previewMap(button: String, action: String) -> [String] {
        ["map", button, action]
    }

    static func applyMap(button: String, action: String) -> [String] {
        ["map", button, action, "--yes"]
    }

    // CPI. Y is optional and the CLI defaults it to X, so the GUI passes it
    // only when the user has actually unticked "X/Y Settings" -- that keeps the
    // common case producing the shorter argv the CLI tests already pin, rather
    // than a second spelling of the same thing.
    static func previewCpi(stage: Int, x: Int, y: Int?) -> [String] {
        var a = ["cpi", String(stage), String(x)]
        if let y = y, y != x { a.append(String(y)) }
        return a
    }

    static func applyCpi(stage: Int, x: Int, y: Int?) -> [String] {
        previewCpi(stage: stage, x: x, y: y) + ["--yes"]
    }

    // Handedness (§7.20). NOT a flag: the CLI reads the record and decides,
    // and refuses a record in neither state. The GUI must therefore never
    // present this as a toggle whose position it already knows -- it runs the
    // preview and shows what the CLI says, the same as every other verb here.
    static func previewHandedness(_ side: String) -> [String] {
        ["handedness", side]
    }

    static func applyHandedness(_ side: String) -> [String] {
        ["handedness", side, "--yes"]
    }

    // Multiclick / SPDT (§7.22). `value` is used only by the "off" mode.
    static func previewMulticlick(button: String, mode: String,
                                  value: Int?) -> [String] {
        var a = ["multiclick", button, mode]
        if mode == "off", let v = value { a.append(String(v)) }
        return a
    }

    static func applyMulticlick(button: String, mode: String,
                                value: Int?) -> [String] {
        previewMulticlick(button: button, mode: mode, value: value) + ["--yes"]
    }

    // Only LEFT and RIGHT have an SPDT combo on the vendor's page, so only they
    // may be offered a GX mode. This mirrors a refusal the CLI makes
    // independently; the GUI's copy exists to keep an impossible choice off the
    // screen, never to decide what is written.
    static let multiclickButtons = ["left", "right", "middle", "forward", "back"]

    static func multiclickModes(for button: String) -> [String] {
        ["left", "right"].contains(button)
            ? ["off", "gx-speed", "gx-safe"] : ["off"]
    }

    static func multiclickIsLegal(mode: String, value: Int?, button: String) -> Bool {
        guard multiclickModes(for: button).contains(mode) else { return false }
        if mode != "off" { return true }
        guard let v = value else { return false }
        return v >= 0 && v <= 25
    }

    // The legal CPI grid, from config-protocol.md §7.19: 10..10000 in steps of
    // 10, then 10050..30000 in steps of 50, rounded half up and clamped. The
    // GUI needs this to disable Apply before a round trip, NOT to decide what
    // gets written -- egg-config re-derives it and refuses independently. Two
    // gates, and the one nearer the wire is the authority.
    static func normaliseCpi(_ v: Int) -> Int {
        let c = min(max(v, 10), 30000)
        let step = c <= 10000 ? 10 : 50
        let q = c / step, rem = c - q * step
        return (rem * 2 >= step ? q + 1 : q) * step
    }

    static func cpiIsLegal(_ v: Int) -> Bool { v == normaliseCpi(v) }

    // -------------------------------------------------------------- egg-flash
    static func listVersions() -> [String] { ["--versions"] }

    static func backupFirmware(to path: String) -> [String] {
        ["read-firmware", path]
    }

    /// Validate the image and stop. Reaches no device.
    static func checkImage(updater: String, version: String?) -> [String] {
        var a = ["image", updater]
        if let v = version, !v.isEmpty { a += ["--version", v] }
        return a
    }

    /// The exact byte stream, printed. Reaches no device.
    static func dryRun(updater: String, version: String?) -> [String] {
        var a = ["dryrun", updater]
        if let v = version, !v.isEmpty { a += ["--version", v] }
        return a
    }

    /// Ask for the confirmation token WITHOUT flashing.
    ///
    /// `flash` with no --confirm refuses, prints "NOTHING WAS SENT", and prints
    /// the token derived from the exact frames it would have sent. That is the
    /// only place the token appears -- `dryrun` prints the byte stream but not
    /// the token -- and the GUI's first version looked for it in `dryrun`
    /// output, found nothing, and would have left a person with a code field
    /// and no code to put in it. Tests/test_app_commands.swift caught that by
    /// running the real tool instead of trusting the format.
    ///
    /// Safe by construction: the refusal happens host-side, before the device
    /// is enumerated (§4.2, "preflight sends nothing").
    static func requestToken(updater: String, backup: String,
                             version: String?) -> [String] {
        var a = ["flash", updater]
        if !backup.isEmpty { a += ["--backup", backup] }
        if let v = version, !v.isEmpty { a += ["--version", v] }
        return a          // deliberately no --yes and no --confirm
    }

    /// Why a flash is not allowed yet, or nil if it is. Returning the REASON
    /// rather than a Bool is what lets the screen tell a person what to do
    /// instead of greying a button out silently.
    static func flashBlockedReason(updater: String,
                                   backup: String,
                                   backupExists: Bool,
                                   token: String,
                                   versionIsProven: Bool,
                                   acknowledgedUntested: Bool,
                                   writeInProgress: Bool) -> String? {
        if writeInProgress {
            return "A firmware write is already running. It cannot be "
                 + "interrupted, and starting a second one is not possible."
        }
        if updater.isEmpty { return "Choose Endgame's updater .exe first." }
        if backup.isEmpty || !backupExists {
            return "Back up the firmware that is on the mouse first (step 1). "
                 + "A saved image is what makes a failed flash recoverable "
                 + "rather than permanent."
        }
        if token.trimmingCharacters(in: .whitespaces).isEmpty {
            return "Run the preview and type the confirmation code it prints. "
                 + "The code is derived from the exact bytes that would be "
                 + "sent, so approving one plan cannot approve a different one."
        }
        if !versionIsProven && !acknowledgedUntested {
            return "This firmware version has never been written to this mouse. "
                 + "Tick the box to say you understand that."
        }
        return nil
    }

    /// The flash itself. Callers must have checked `flashBlockedReason` first;
    /// this asserts it rather than trusting it, because a view that forgets is
    /// a view that erases a mouse.
    static func flash(updater: String,
                      backup: String,
                      token: String,
                      version: String?,
                      versionIsProven: Bool) -> [String] {
        precondition(!updater.isEmpty && !backup.isEmpty && !token.isEmpty,
                     "Commands.flash called without updater, backup and token")
        var a = ["flash", updater,
                 "--backup", backup,
                 "--confirm", token.trimmingCharacters(in: .whitespaces),
                 "--yes"]
        if let v = version, !v.isEmpty { a += ["--version", v] }
        if !versionIsProven { a.append("--i-know-this-version-is-untested") }
        return a
    }

    // ------------------------------------------------------------------ parse
    /// The eight hex characters `egg-flash dryrun` prints after "--confirm".
    /// Shown to a person, deliberately never auto-filled (§4.2c: "no approval
    /// can be given for something the approver has not been shown").
    static func confirmToken(in text: String) -> String? {
        guard let r = text.range(of: "--confirm ") else { return nil }
        let tail = text[r.upperBound...].prefix(8)
        guard tail.count == 8, tail.allSatisfy({ $0.isHexDigit }) else { return nil }
        return String(tail)
    }

    /// One settable field, as `egg-config set` describes it.
    struct Field: Equatable {
        let name: String
        let record: String
        let accepts: String
        let cite: String
    }

    /// Parse `egg-config set` with no arguments.
    ///
    /// The format is two lines per field -- "  name  record 0xNN  accepts ..."
    /// then an indented citation -- followed by a "deliberately NOT settable"
    /// section that must NOT be offered as editable, and a trailing paragraph.
    ///
    /// Parsed rather than duplicated because the CLI's table is the one cited to
    /// cfg107 addresses and checked against raw bytes by
    /// Tests/test_citations.py. A second copy here would drift, and the first
    /// symptom would be the GUI offering a field the tool refuses -- or naming
    /// one after the wrong record.
    /// Which fields `egg-config read` says THIS device will not accept, and why.
    ///
    /// The GUI must not offer a choice the tool will reject -- the same rule
    /// that keeps `left` and `cpi-button` out of the button picker. §7.25 adds
    /// a second kind of rejection: a field whose MEANING depends on another
    /// record byte. The reason is parsed rather than reconstructed, and no
    /// record offset appears anywhere in this file, because a second copy of
    /// the gate table here is exactly the drift ConfigView.swift's header warns
    /// about.
    ///
    /// `read` prints either
    ///     fields this device will NOT accept: none -- ...
    /// or
    ///     fields this device will NOT accept:
    ///       lod              <reason>
    /// and an empty result means "nothing gated", which is also what a caller
    /// gets from output that has no such section at all. That is deliberate:
    /// an older egg-config prints no section, and the GUI must not respond to
    /// that by disabling everything.
    static func parseGates(_ text: String) -> [String: String] {
        var out: [String: String] = [:]
        var inSection = false
        for raw in text.split(separator: "\n", omittingEmptySubsequences: false) {
            let line = String(raw)
            if line.hasPrefix("fields this device will NOT accept:") {
                inSection = !line.contains("none --")
                continue
            }
            guard inSection else { continue }
            guard line.hasPrefix("  ") else { inSection = false; continue }
            let body = line.trimmingCharacters(in: .whitespaces)
            if body.isEmpty { inSection = false; continue }
            guard let sp = body.range(of: "  ") else { continue }
            let name = String(body[body.startIndex..<sp.lowerBound])
            let why = String(body[sp.upperBound...]).trimmingCharacters(in: .whitespaces)
            if !name.isEmpty && !why.isEmpty { out[name] = why }
        }
        return out
    }

    static func parseFields(_ text: String) -> (fields: [Field],
                                                withheld: [(String, String)]) {
        var fields: [Field] = []
        var withheld: [(String, String)] = []
        var section = 0
        var pendingName = "", pendingRecord = "", pendingAccepts = ""
        var havePending = false

        func flush(cite: String) {
            guard havePending else { return }
            fields.append(Field(name: pendingName, record: pendingRecord,
                                accepts: pendingAccepts, cite: cite))
            havePending = false
        }

        for raw in text.split(separator: "\n", omittingEmptySubsequences: false) {
            let line = String(raw)
            if line.hasPrefix("derived, but deliberately NOT settable") {
                flush(cite: ""); section = 1; continue
            }
            if line.hasPrefix("Everything else") { flush(cite: ""); section = 2; continue }
            guard line.hasPrefix("  ") else { continue }
            let body = line.trimmingCharacters(in: .whitespaces)
            if body.isEmpty { continue }

            if section == 0 {
                if let recRange = body.range(of: "record "),
                   let accRange = body.range(of: "accepts "),
                   recRange.lowerBound < accRange.lowerBound {
                    flush(cite: "")
                    pendingName = String(body[body.startIndex..<recRange.lowerBound])
                        .trimmingCharacters(in: .whitespaces)
                    pendingRecord = String(body[recRange.lowerBound..<accRange.lowerBound])
                        .trimmingCharacters(in: .whitespaces)
                    pendingAccepts = String(body[accRange.upperBound...])
                        .trimmingCharacters(in: .whitespaces)
                    havePending = true
                } else if havePending {
                    flush(cite: body)
                }
            } else if section == 1 {
                if let sp = body.range(of: "  ") {
                    withheld.append((String(body[body.startIndex..<sp.lowerBound]),
                                     String(body[sp.upperBound...])
                                        .trimmingCharacters(in: .whitespaces)))
                }
            }
        }
        flush(cite: "")
        return (fields, withheld)
    }

    /// The choices a field offers, or nil when it takes free text.
    static func choices(for accepts: String) -> [String]? {
        if accepts.hasPrefix("0 or 1") { return ["0", "1"] }
        guard accepts.contains(",") || accepts.contains(" or ") else { return nil }
        let head = accepts.split(separator: "(").first.map(String.init) ?? accepts
        let parts = head.replacingOccurrences(of: " or ", with: ", ")
            .split(separator: ",")
            .map { $0.trimmingCharacters(in: .whitespaces) }
        guard parts.count > 1, parts.allSatisfy({ Int($0) != nil }) else { return nil }
        return parts
    }

    /// Release labels from `egg-flash --versions`, and which one is proven.
    static func parseVersions(_ text: String) -> (labels: [String], proven: String?) {
        var labels: [String] = []
        var proven: String?
        for line in text.split(separator: "\n", omittingEmptySubsequences: false) {
            guard line.hasPrefix("  "), line.contains("version resource") else { continue }
            let t = line.trimmingCharacters(in: .whitespaces)
            guard let first = t.split(separator: " ").first else { continue }
            labels.append(String(first))
            if t.contains("VERIFIED ON THIS MOUSE") { proven = String(first) }
        }
        return (labels, proven)
    }
}
