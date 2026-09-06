// Tests/test_app_commands.swift -- the GUI's command builders, driven against
// the real CLIs.
//
// The app's entire correctness obligation is building the right argument list
// (Sources/EGGApp/ToolRunner.swift explains why it has no others). So this test
// does two things a unit test of string concatenation would not:
//
//   1. It RUNS the arguments. A command line that egg-config or egg-flash
//      rejects is a bug in the GUI no amount of eyeballing catches, and the
//      failure mode -- a greyed button or a confusing error -- is one a person
//      would report as "the app is broken", not as "the arguments were wrong".
//
//   2. It asserts that the read-only builders REACH NO DEVICE and the writing
//      ones carry every guard. `flashBlockedReason` is checked case by case,
//      including the case that matters most: a flash with no backup file.
//
// Run standalone: swift Tests/test_app_commands.swift
// Under ctest:    ctest -R app_commands
import Foundation

// The file under test is compiled in alongside this one by the build script.

var failures = 0
var checks = 0

func check(_ cond: Bool, _ what: String, _ detail: @autoclosure () -> String = "") {
    checks += 1
    if !cond {
        failures += 1
        let d = detail()
        FileHandle.standardError.write(
            Data(("FAIL: \(what)" + (d.isEmpty ? "" : "\n      " + d) + "\n")
                    .utf8))
    }
}

func equal<T: Equatable>(_ got: T, _ want: T, _ what: String) {
    check(got == want, what, "got \(got)\n      want \(want)")
}

// --------------------------------------------------------------------------
// 1. Argument shapes
// --------------------------------------------------------------------------
equal(Commands.readSettings(), ["read"], "read with no save path")
equal(Commands.readSettings(savingTo: "/tmp/x.bin"),
      ["read", "--save", "/tmp/x.bin"], "read with a save path")
equal(Commands.readSettings(savingTo: ""), ["read"],
      "an empty save path must not produce a bare --save")

equal(Commands.previewSet(field: "polling", value: "1000"),
      ["set", "polling", "1000"], "preview carries no --yes")
check(!Commands.previewSet(field: "polling", value: "1000").contains("--yes"),
      "a PREVIEW must never contain --yes")
equal(Commands.applySet(field: "polling", value: "1000"),
      ["set", "polling", "1000", "--yes"], "apply carries --yes")

check(!Commands.previewMap(button: "forward", action: "volume-up").contains("--yes"),
      "a map PREVIEW must never contain --yes")
check(Commands.applyMap(button: "forward", action: "volume-up").contains("--yes"),
      "an applied map must carry --yes")

equal(Commands.checkImage(updater: "u.exe", version: nil), ["image", "u.exe"],
      "checkImage with no version")
equal(Commands.checkImage(updater: "u.exe", version: "1.07"),
      ["image", "u.exe", "--version", "1.07"], "checkImage with a version")
equal(Commands.dryRun(updater: "u.exe", version: ""), ["dryrun", "u.exe"],
      "an empty version must not produce a bare --version")

// --------------------------------------------------------------------------
// 2. The flash gate, case by case
// --------------------------------------------------------------------------
func blocked(updater: String = "u.exe", backup: String = "b.bin",
             exists: Bool = true, token: String = "deadbeef",
             proven: Bool = true, ack: Bool = false,
             writing: Bool = false) -> String? {
    Commands.flashBlockedReason(updater: updater, backup: backup,
                                backupExists: exists, token: token,
                                versionIsProven: proven,
                                acknowledgedUntested: ack,
                                writeInProgress: writing)
}

check(blocked() == nil, "a complete, proven, backed-up flash is allowed")
check(blocked(updater: "") != nil, "no updater must block")
check(blocked(exists: false) != nil,
      "NO BACKUP FILE MUST BLOCK -- §4.2 never erase without a saved copy")
check(blocked(backup: "") != nil, "an empty backup path must block")
check(blocked(token: "") != nil, "no confirmation token must block (§4.2c)")
check(blocked(token: "   ") != nil, "a whitespace token must block")
check(blocked(proven: false, ack: false) != nil,
      "an unproven firmware version must block without acknowledgement (§5)")
check(blocked(proven: false, ack: true) == nil,
      "an unproven version with acknowledgement is allowed")
check(blocked(writing: true) != nil,
      "a second flash must block while one is running")

// Every blocking reason must be a sentence a person can act on, not a code.
for r in [blocked(updater: ""), blocked(exists: false), blocked(token: ""),
          blocked(proven: false), blocked(writing: true)] {
    check((r ?? "").count > 30 && (r ?? "").hasSuffix("."),
          "a blocking reason must be a usable sentence", r ?? "nil")
}

// --------------------------------------------------------------------------
// 3. The flash argument list carries every guard
// --------------------------------------------------------------------------
let f = Commands.flash(updater: "u.exe", backup: "b.bin", token: " c0ffee12 ",
                       version: "1.10", versionIsProven: true)
check(f.contains("--yes"), "flash carries --yes")
check(f.contains("--backup") && f.contains("b.bin"), "flash names the backup")
check(f.contains("--confirm"), "flash carries --confirm")
check(f.contains("c0ffee12"), "the token is trimmed, not passed with spaces")
check(!f.contains(" c0ffee12 "), "the token must be trimmed")
check(!f.contains("--i-know-this-version-is-untested"),
      "a PROVEN version must not carry the untested acknowledgement")

let g = Commands.flash(updater: "u.exe", backup: "b.bin", token: "c0ffee12",
                       version: "1.04", versionIsProven: false)
check(g.contains("--i-know-this-version-is-untested"),
      "an UNPROVEN version must carry the untested acknowledgement")

// --------------------------------------------------------------------------
// 4. Parsing, against the real tools' real output
// --------------------------------------------------------------------------
func repoRoot() -> URL {
    var d = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
    for _ in 0..<4 {
        if FileManager.default.fileExists(
            atPath: d.appendingPathComponent("CLAUDE.md").path) { return d }
        d = d.deletingLastPathComponent()
    }
    return URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
}

@discardableResult
func run(_ tool: String, _ args: [String]) -> (Int32, String) {
    let exe = repoRoot().appendingPathComponent("build/\(tool)")
    guard FileManager.default.isExecutableFile(atPath: exe.path) else {
        return (-1, "")
    }
    let p = Process()
    p.executableURL = exe
    p.arguments = args
    p.currentDirectoryURL = repoRoot()
    let out = Pipe(), err = Pipe()
    p.standardOutput = out
    p.standardError = err
    do { try p.run() } catch { return (-2, "\(error)") }
    let o = String(data: out.fileHandleForReading.readDataToEndOfFile(),
                   encoding: .utf8) ?? ""
    let e = String(data: err.fileHandleForReading.readDataToEndOfFile(),
                   encoding: .utf8) ?? ""
    p.waitUntilExit()
    return (p.terminationStatus, o + e)
}

let (vs, versionsText) = run("egg-flash", Commands.listVersions())
if vs == -1 {
    print("SKIP: build/egg-flash not present; argument shapes checked only")
} else {
    let parsed = Commands.parseVersions(versionsText)
    check(!parsed.labels.isEmpty,
          "parseVersions found no releases in egg-flash --versions",
          versionsText.prefix(200).description)
    check(parsed.proven != nil,
          "parseVersions found no PROVEN release; the GUI would then default "
          + "to an untested firmware")
    check(parsed.labels.contains(parsed.proven ?? ""),
          "the proven release must be one of the listed labels")
    check(Set(parsed.labels).count == parsed.labels.count,
          "duplicate release labels")

    // The read-only builders must actually be accepted by the tool.
    let updater = repoRoot()
        .appendingPathComponent("Endgame Gear OP1 8k v2 Firmware Updater 1.10.exe")
    if FileManager.default.fileExists(atPath: updater.path) {
        let (s1, t1) = run("egg-flash",
                           Commands.checkImage(updater: updater.path,
                                               version: parsed.proven))
        check(s1 == 0, "the GUI's `image` arguments were rejected by egg-flash", t1)
        check(t1.contains("sha256"), "`image` did not report a hash", t1)

        let (s2, t2) = run("egg-flash",
                           Commands.dryRun(updater: updater.path,
                                           version: parsed.proven))
        check(s2 == 0, "the GUI's `dryrun` arguments were rejected",
              t2.prefix(300).description)
        check(t2.contains("A0 06 write"),
              "`dryrun` did not print the byte stream the GUI shows", t2)

        // The token comes from `flash` with no --confirm, which refuses
        // host-side and prints it. The GUI's first version looked in `dryrun`
        // and found nothing; only running the real tool caught that.
        let (_, t3) = run("egg-flash",
                          Commands.requestToken(updater: updater.path,
                                                backup: "/nonexistent.bin",
                                                version: parsed.proven))
        check(t3.contains("NOTHING WAS SENT"),
              "requestToken's command must be a refusal, not a flash", t3)
        let tok = Commands.confirmToken(in: t3)
        check(tok != nil,
              "confirmToken could not find the code in the real refusal -- the "
              + "GUI would show a person a code field and no code (§4.2c)")
        if let tok {
            check(tok.count == 8 && tok.allSatisfy { $0.isHexDigit },
                  "the parsed token is not eight hex digits", tok)
            // The same plan must give the same token every time (§4.3).
            let (_, again) = run("egg-flash",
                                 Commands.requestToken(updater: updater.path,
                                                       backup: "/nonexistent.bin",
                                                       version: parsed.proven))
            equal(Commands.confirmToken(in: again), tok,
                  "the token must be identical for an identical plan")
        }
        check(!Commands.requestToken(updater: "u", backup: "b", version: nil)
                .contains("--yes"),
              "requestToken must never carry --yes")
        check(!Commands.requestToken(updater: "u", backup: "b", version: nil)
                .contains("--confirm"),
              "requestToken must never carry --confirm")
    } else {
        print("SKIP: updater .exe not present; live flash-argument checks skipped")
    }
}

let (cs, setText) = run("egg-config", ["set"])
if cs == -1 {
    print("SKIP: build/egg-config not present; field parsing checked on a "
          + "fixture only")
} else {
    let p = Commands.parseFields(setText)
    check(p.fields.count >= 10,
          "parseFields found only \(p.fields.count) settable fields in real "
          + "egg-config output; the Settings screen would show almost nothing",
          setText.prefix(400).description)
    check(p.fields.contains { $0.name == "polling" }, "polling not parsed")
    check(p.fields.contains { $0.name == "lod" }, "lod not parsed")
    check(p.fields.contains { $0.name == "sensor-angle" }, "sensor-angle not parsed")

    for f in p.fields {
        check(!f.name.isEmpty && !f.name.contains(" "),
              "a field name must be one word", f.name)
        check(f.record.hasPrefix("record 0x"),
              "field \(f.name) has a malformed record column", f.record)
        check(!f.accepts.isEmpty, "field \(f.name) has no accepts text")
        // Every settable field must carry a citation (CLAUDE.md §1.2). The
        // CLI enforces this too; the GUI showing a blank there would hide it.
        check(f.cite.contains("cfg1"),
              "field \(f.name) shows no cfg1xx citation in the GUI", f.cite)
    }

    // The withheld list must NOT leak into the editable set: offering one
    // would mean offering a byte whose meaning is [G] (§1.3).
    for (name, _) in p.withheld {
        check(!p.fields.contains { $0.name == name },
              "\(name) is listed as NOT settable and also offered as editable")
    }
    check(p.withheld.count >= 2,
          "the withheld list was not parsed; the GUI would silently drop the "
          + "explanation of why some settings are missing")

    // Control types, against the real acceptance strings.
    let polling = p.fields.first { $0.name == "polling" }!
    let pc = Commands.choices(for: polling.accepts)
    check(pc != nil && pc!.contains("8000") && pc!.contains("125"),
          "polling should offer a list of rates", "\(pc ?? [])")
    let toggle = p.fields.first { $0.name == "angle-snapping" }
    if let t = toggle {
        equal(Commands.choices(for: t.accepts), ["0", "1"],
              "a 0-or-1 field must become a two-way control")
    }
    let angle = p.fields.first { $0.name == "sensor-angle" }!
    check(Commands.choices(for: angle.accepts) == nil,
          "sensor-angle is a range, not a list; it must stay free text",
          "\(Commands.choices(for: angle.accepts) ?? [])")
}

// --------------------------------------------------------------------------
// 5. The parsers refuse what they should (§6.2: a harness that cannot produce
//    a bad result is not evidence). These are fixtures, not the real tools, so
//    they run even when nothing is built.
// --------------------------------------------------------------------------
let noCite = """
settable fields (only those whose MEANING is derived, §1.3):
  polling          record 0x05        accepts 125, 250 or 500 (Hz)
                   windows-run/02-basic; counted the writes
"""
let nc = Commands.parseFields(noCite)
equal(nc.fields.count, 1, "a fixture with one field parses to one field")
check(!nc.fields[0].cite.contains("cfg1"),
      "a citation-free field must not be reported as cited -- otherwise the "
      + "check above that every field shows a citation proves nothing")

let mixed = """
settable fields (only those whose MEANING is derived, §1.3):
  polling          record 0x05        accepts 0 or 1
                   config-protocol.md §7.5, cfg107 0x413a79
derived, but deliberately NOT settable yet:
  button-mapping           because reasons
"""
let mx = Commands.parseFields(mixed)
equal(mx.fields.map(\.name), ["polling"], "only the settable field is editable")
equal(mx.withheld.map(\.0), ["button-mapping"], "the withheld field is withheld")

check(Commands.parseFields("total nonsense\nwith no structure").fields.isEmpty,
      "garbage must parse to no fields, not to a plausible-looking one")

check(Commands.choices(for: "-127 to 127 (degrees, two's complement)") == nil,
      "a signed range must not be turned into a picker")
check(Commands.choices(for: "0 to 10 (eleven lift-off distance steps)") == nil,
      "an N-to-M range must not be turned into a picker")

check(Commands.confirmToken(in: "--confirm zzzzzzzz") == nil,
      "a non-hex token must be rejected")
check(Commands.confirmToken(in: "--confirm abc") == nil,
      "a short token must be rejected")
check(Commands.confirmToken(in: "no token here") == nil,
      "no token means nil, not an empty string")
equal(Commands.confirmToken(in: "--confirm ecfc8f88\n"), "ecfc8f88",
      "a well-formed token is read")

equal(Commands.parseVersions("nothing here").labels, [],
      "parseVersions must find nothing in unrelated text")

// -------------------------------------------------------------------- CPI
// The GUI carries its OWN copy of the CPI grid so it can grey out Apply before
// a round trip. A second copy of a rule is a second thing to get wrong, so it
// is scored against the CLI rather than against itself: for every value below,
// whether Commands.cpiIsLegal says yes must match whether egg-config actually
// encodes it. If the two ever disagree the GUI is lying to the user about what
// the device will accept.
equal(Commands.previewCpi(stage: 1, x: 1600, y: nil), ["cpi", "1", "1600"],
      "a symmetric CPI must not grow a redundant Y argument")
equal(Commands.previewCpi(stage: 1, x: 1600, y: 1600), ["cpi", "1", "1600"],
      "Y equal to X is the same command, not a longer one")
equal(Commands.previewCpi(stage: 4, x: 1600, y: 800),
      ["cpi", "4", "1600", "800"], "an asymmetric CPI passes both")
check(!Commands.previewCpi(stage: 1, x: 800, y: nil).contains("--yes"),
      "previewCpi must never carry --yes")
check(Commands.applyCpi(stage: 1, x: 800, y: nil).contains("--yes"),
      "applyCpi must carry --yes")

let cpiProbe = [1, 9, 10, 11, 15, 400, 405, 800, 1600, 1605, 9995, 10000,
                10005, 10050, 12030, 29999, 30000, 30001, 100000]
let (cpiStatus, _) = run("egg-config", ["cpi"])
if cpiStatus == -1 {
    print("SKIP: build/egg-config not present; CPI grid not scored against it")
} else {
    for v in cpiProbe {
        let (_, text) = run("egg-config", ["cpi", "1", String(v)])
        let cliAccepts = text.contains("would write record")
        check(Commands.cpiIsLegal(v) == cliAccepts,
              "CPI \(v): GUI says \(Commands.cpiIsLegal(v) ? "legal" : "illegal"), "
              + "egg-config \(cliAccepts ? "accepted" : "refused") it")
    }
    // And the rounding target the GUI would show must be the one the CLI names.
    let (_, r) = run("egg-config", ["cpi", "1", "1605"])
    check(r.contains("would store \(Commands.normaliseCpi(1605))"),
          "the GUI and the CLI must round 1605 to the same place", r)
}

// ------------------------------------------------ handedness and multiclick
equal(Commands.previewHandedness("left"), ["handedness", "left"],
      "previewHandedness must not carry --yes")
equal(Commands.applyHandedness("left"), ["handedness", "left", "--yes"],
      "applyHandedness must carry --yes")

equal(Commands.previewMulticlick(button: "left", mode: "off", value: 12),
      ["multiclick", "left", "off", "12"], "off carries its value")
equal(Commands.previewMulticlick(button: "left", mode: "gx-safe", value: 12),
      ["multiclick", "left", "gx-safe"],
      "a GX mode must not carry a filter value the CLI would ignore")
equal(Commands.multiclickModes(for: "left"), ["off", "gx-speed", "gx-safe"],
      "left has an SPDT combo")
equal(Commands.multiclickModes(for: "middle"), ["off"],
      "middle has no SPDT combo")

// Scored against the CLI, not against itself -- the GUI's copy of the rule
// exists to grey out a button, and if it ever disagrees with the tool the user
// is being told the wrong thing about their own mouse.
let (mcStatus, _) = run("egg-config", ["multiclick"])
if mcStatus == -1 {
    print("SKIP: build/egg-config not present; multiclick rules not scored")
} else {
    for b in Commands.multiclickButtons {
        for m in ["off", "gx-speed", "gx-safe", "nonsense"] {
            for v in [-1, 0, 8, 25, 26] {
                if m != "off" && v != 8 { continue }
                var argv = ["multiclick", b, m]
                if m == "off" { argv.append(String(v)) }
                let (_, text) = run("egg-config", argv)
                let cli = text.contains("would write 0x")
                let gui = Commands.multiclickIsLegal(mode: m,
                                                     value: m == "off" ? v : nil,
                                                     button: b)
                check(gui == cli,
                      "multiclick \(b) \(m) \(v): GUI says \(gui ? "legal" : "illegal"), "
                      + "egg-config \(cli ? "accepted" : "refused")")
            }
        }
    }
}

// --------------------------------------------------------------------------
print("\(checks - failures)/\(checks) checks passed")
if failures > 0 { exit(1) }
