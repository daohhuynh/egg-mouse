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
