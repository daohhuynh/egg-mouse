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

equal(Commands.previewRestore(record: "/tmp/r.bin"), ["dryrun", "/tmp/r.bin"],
      "a restore PREVIEW is the offline dryrun")
check(!Commands.previewRestore(record: "/tmp/r.bin").contains("--yes"),
      "a restore PREVIEW must never contain --yes")
equal(Commands.applyRestore(record: "/tmp/r.bin"),
      ["restore", "/tmp/r.bin", "--yes"], "an applied restore carries --yes")
check(Commands.applyRestore(record: "/tmp/r.bin").contains("/tmp/r.bin"),
      "the applied restore must name the SAME file the preview was given")

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

// The two overrides are SEPARATE flags and must never leak into each other.
// They are different admissions -- "this firmware is unproven" and "I used the
// buttons" -- and a builder that emitted one for the other would be approving
// something the person never said.
check(!f.contains("--i-know-this-is-button-entered"),
      "flash must not carry the button-entry override unless asked")
let fb = Commands.flash(updater: "u.exe", backup: "b.bin", token: "c0ffee12",
                        version: "1.10", versionIsProven: true,
                        buttonEntered: true)
check(fb.contains("--i-know-this-is-button-entered"),
      "flash carries the button-entry override when the box is ticked")
check(!fb.contains("--i-know-this-version-is-untested"),
      "ticking button-entry must NOT also claim the version is untested")

// requestToken needs the override too, and that is not obvious: egg-flash
// checks the entry receipt BEFORE it prints the token, so a preview against a
// button-entered mouse is refused and produces no code at all. Without this the
// app could never reach the confirm field.
check(!Commands.requestToken(updater: "u", backup: "b", version: nil)
        .contains("--i-know-this-is-button-entered"),
      "requestToken must not carry the override unless asked")
check(Commands.requestToken(updater: "u", backup: "b", version: nil,
                            buttonEntered: true)
        .contains("--i-know-this-is-button-entered"),
      "requestToken carries the override when the box is ticked")

// enter-bootloader: the app could not reach update mode at all before this.
let eb = Commands.enterBootloader()
equal(eb, ["enter-bootloader", "--yes"], "enterBootloader is the exact verb")
// An app that can enter a mode it cannot leave is worse than one that can do
// neither, so these two ship together or not at all.
equal(Commands.leaveBootloader(), ["leave-bootloader", "--yes"],
      "leaveBootloader is the exact verb")

// restore-firmware carries every guard the flash does.
let rf = Commands.restoreFirmware(backup: "old.bin", current: "now.bin",
                                  token: " deadbeef ")
check(rf.first == "restore-firmware", "restoreFirmware names the right verb")
check(rf.contains("old.bin"), "the image to WRITE is the positional argument")
check(rf.contains("--backup") && rf.contains("now.bin"),
      "--backup is the FRESH read of what is on the device now")
check(rf.contains("--confirm") && rf.contains("deadbeef"),
      "restoreFirmware carries a trimmed --confirm")
check(rf.contains("--yes"), "restoreFirmware carries --yes")
check(!Commands.requestRestoreToken(backup: "old.bin", current: "now.bin")
        .contains("--confirm"),
      "the restore PREVIEW must never carry --confirm")
check(!Commands.requestRestoreToken(backup: "old.bin", current: "now.bin")
        .contains("--yes"),
      "the restore PREVIEW must never carry --yes")

// --------------------------------------------------------------------------
// 4. Parsing, against the real tools' real output
// --------------------------------------------------------------------------
/// The 1040-byte factory-default record, REBUILT from the capture if absent.
///
/// `frames/` is not in the repository -- .gitignore excludes `*.bin` -- but
/// every byte of it is committed inside `windows-run/01-baseline.pcapng`.
/// `Tests/test_flash_undo.sh` has rebuilt rather than skipped since 2026-09-07;
/// this file still skipped, so on a fresh clone three checks below quietly
/// became no-ops and the run still said PASS. A check that did not run reads
/// exactly like a check that passed, which is the failure this whole suite
/// exists to avoid.
func frameFixture() -> URL {
    let p = repoRoot().appendingPathComponent("frames/01-baseline-003-in-01.bin")
    if !FileManager.default.fileExists(atPath: p.path) {
        let t = Process()
        t.executableURL = URL(fileURLWithPath: "/usr/bin/env")
        t.arguments = ["python3", "Tools/capture/mkframes.py"]
        t.currentDirectoryURL = repoRoot()
        t.standardOutput = FileHandle.nullDevice
        t.standardError = FileHandle.nullDevice
        try? t.run()
        t.waitUntilExit()
    }
    return p
}

func repoRoot() -> URL {
    // CMakeLists.txt, not engineering-rules.md: the latter stopped being committed on
    // 2026-09-07, so it is absent in a fresh clone and this would have walked
    // past the real root and fallen back to the cwd.
    var d = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
    for _ in 0..<4 {
        if FileManager.default.fileExists(
            atPath: d.appendingPathComponent("CMakeLists.txt").path) { return d }
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
        // Every settable field must carry a citation (engineering-rules.md §1.2). The
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

// --------------------------------------------------------------------------
// The three readers that turn an `accepts` sentence into a control
// --------------------------------------------------------------------------
// Each one exists so a protocol fact is READ from the CLI's own wording rather
// than written down a second time in the view. That only holds if a wording
// change makes them return nil -- a fallback to a plain number box -- instead
// of returning something confident and wrong. So every check below has its
// negative twin.
check(Commands.range(for: "0 to 10 -- lift-off distance, 0 = 0.7mm up to "
                        + "10 = 1.7mm in 0.1mm steps") == 0...10,
      "lod's bounds come out of its own sentence")
check(Commands.range(for: "-127 to 127 (degrees, two's complement)") == -127...127,
      "a signed range parses, sign and all")
check(Commands.range(for: "1 to 4, the dropdown item counting from the top "
                        + "(stored remapped: 1->2, 2->3, 3->1, 4->0)") == 1...4,
      "the remapped fields expose the ITEM range, not the stored bytes")
check(Commands.range(for: "125, 250, 500, 1000, 2000, 4000 or 8000 (Hz)") == nil,
      "a list is not a range")
check(Commands.range(for: "0 or 1") == nil, "a flag is not a range")
check(Commands.range(for: "10 to 4") == nil, "a backwards range is refused")

let lodLabels = Commands.rangeLabels(
    for: "0 to 10 -- lift-off distance, 0 = 0.7mm up to 10 = 1.7mm in 0.1mm steps")
check(lodLabels?[0] == "0.7 mm", "lod 0 is labelled 0.7 mm",
      lodLabels?[0] ?? "nil")
check(lodLabels?[10] == "1.7 mm", "lod 10 is labelled 1.7 mm",
      lodLabels?[10] ?? "nil")
check(lodLabels?[3] == "1.0 mm",
      "lod 3 interpolates to 1.0 mm, which is what the vendor's dropdown shows "
      + "on the same record (basic.png)", lodLabels?[3] ?? "nil")
equal(lodLabels?.count, 11, "one label per step, no more")
// The twin. Change the wording and the labels must disappear, not degrade.
check(Commands.rangeLabels(for: "0 to 10 -- lift-off distance in steps") == nil,
      "a sentence that names no endpoints yields no labels")
check(Commands.rangeLabels(for: "-127 to 127 (degrees, two's complement)") == nil,
      "a range with no millimetres in it yields no labels")

equal(Commands.unitSuffix(for: "125, 250, 500, 1000, 2000, 4000 or 8000 (Hz)"),
      "Hz", "the polling picker can say Hz because the CLI does")
check(Commands.unitSuffix(for: "-127 to 127 (degrees, two's complement)") == nil,
      "a parenthetical that is prose is not a unit")
check(Commands.unitSuffix(for: "0 or 1") == nil, "no parenthetical, no unit")
check(Commands.unitSuffix(for: "1 to 4, the dropdown item counting from the "
                             + "top (stored remapped: 1->2, 2->3, 3->1, 4->0)")
        == nil,
      "a long parenthetical is not a unit")

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

// -------------------------------------------------------------- §7.25 gates
// The GUI greys a field out on the strength of THIS parse, so a parse that
// silently returns nothing would re-offer a field the tool refuses -- and the
// user would only find out after typing a value and pressing Apply.

let gatedText = """
settings record, 1024 payload bytes at +0x10:

0000  a0 11 00 00
saved 1041 bytes

fields this device will NOT accept:
  lod              record 0x6f is not 0, and it decides what `lod` means. See §7.25

known-good record: held
"""
let gates = Commands.parseGates(gatedText)
check(gates.count == 1, "one gated field, got \(gates.count)")
check(gates["lod"] != nil, "the gated field is named")
check(gates["lod"]?.contains("0x6f") == true, "the reason survives the parse")

check(Commands.parseGates("""
fields this device will NOT accept: none -- every derived field is settable on it.
""").isEmpty, "`none` must parse as nothing gated, not as a field called none")

// AN OLDER egg-config PRINTS NO SUCH SECTION. The GUI must read that as
// "nothing gated" and keep working, never as "everything gated".
check(Commands.parseGates("settings record, 1024 payload bytes at +0x10:\n\n0000  a0 11\n").isEmpty,
      "output with no gate section leaves every field enabled")

// The section ends at the first line that is not an indented entry. Without
// this the vault line below it would be parsed as a gated field named
// `known-good`.
check(gates["known-good"] == nil && gates["record:"] == nil,
      "the section stops at the blank line; nothing after it is a field")

// A field name longer than the CLI's pad width. `%-16s %s` would collapse to a
// single space here and this parse would return nothing at all -- silently
// re-enabling a field the device refuses. The CLI pads to 20 and always emits
// two spaces; this is the check that keeps it honest.
let longName = Commands.parseGates("""
fields this device will NOT accept:
  motion-jitter-filter  a twenty-character name still separates cleanly
""")
check(longName["motion-jitter-filter"] != nil,
      "a name at the pad width still parses; got \(longName.keys.sorted())")

// Scored against the CLI's real shape, offline -- `dryrun` refuses a gated
// field and `read` prints the section, so a record with 0x6f poked to 1 is the
// only fixture needed and it needs no mouse.
let (dStatus, dText) = run("egg-config", ["set"])
if dStatus == -1 {
    print("SKIP: build/egg-config not present; gate wording not scored")
} else {
    check(!dText.contains("fields this device will NOT accept"),
          "`set` lists fields; only `read` knows what a DEVICE will accept")
}

// --------------------------------------------------------------------------
// Restore, driven against the real egg-config with NO DEVICE ATTACHED.
//
// This is the pair engineering-rules.md 4.1 asks for: the GUI could already factory-reset
// the mouse and save a copy, and until 2026-09-06 it could not put the copy
// back. The preview half is the interesting one to test, because it is the
// half that must work with the mouse unplugged -- if `dryrun` ever needed the
// device, the GUI's two-step flow would silently become one step.
// --------------------------------------------------------------------------
do {
    let frame = frameFixture()
    if let raw = FileManager.default.contents(atPath: frame.path) {
        var rec = [UInt8](raw)
        if rec.count < 1041 { rec += [UInt8](repeating: 0, count: 1041 - rec.count) }
        let tmp = FileManager.default.temporaryDirectory
            .appendingPathComponent("egg-restore-preview.bin")
        try? Data(rec[0..<1041]).write(to: tmp)

        let (rc, text) = run("egg-config", Commands.previewRestore(record: tmp.path))
        if rc == -1 {
            print("SKIP: build/egg-config not present; restore shapes checked only")
        } else {
            check(rc == 0, "the restore preview must succeed offline",
                  "rc \(rc)\n      " + text.prefix(300).description)
            check(text.contains("restore"),
                  "the preview must say it is previewing a RESTORE", text.prefix(200).description)
            check(text.contains("command 0x11"),
                  "the preview must name the A0 11 write frame it would send",
                  text.prefix(300).description)
            // Assert the REASSURANCE, not the absence of a scary word. The
            // first version of this check failed on `dryrun`'s own closing
            // line -- "Nothing was sent. No device was opened." -- by
            // grepping for "no device". Searching for the string a FAILURE
            // would contain is the weaker test anyway: it passes on any
            // output that happens not to use those words.
            check(text.contains("Nothing was sent")
                  && text.contains("No device was opened"),
                  "the preview must SAY it opened no device, so the person "
                  + "approving the second click can see the first was free",
                  text.suffix(200).description)
        }

        // The GUI hardcodes the vault's name. If egg-config ever moves it,
        // the "Restore known-good" button silently stops appearing -- a
        // disappearing undo, which is the worst way for this to fail. The
        // CLI prints the default in its own help, so compare against that.
        let (_, help) = run("egg-config", [])
        check(help.contains(Commands.knownGoodVaultName),
              "egg-config's help no longer names \(Commands.knownGoodVaultName); "
              + "the GUI's known-good button is pointing at the wrong file",
              help.prefix(400).description)
        check(Commands.knownGoodVaultPath().hasSuffix(Commands.knownGoodVaultName),
              "the vault path must end in the vault name")
        check(Commands.knownGoodVaultPath().hasPrefix("/"),
              "the vault path must be absolute -- an open panel cannot use a "
              + "relative one")

        // A file that is not a record must be REFUSED, not previewed: the GUI
        // arms its write button on the preview's exit status alone.
        let junk = FileManager.default.temporaryDirectory
            .appendingPathComponent("egg-restore-junk.bin")
        try? Data([0x00, 0x01, 0x02]).write(to: junk)
        let (jrc, _) = run("egg-config", Commands.previewRestore(record: junk.path))
        check(jrc != 0, "a 3-byte file must not preview as a restorable record")
    } else {
        check(false, "frames/01-baseline-003-in-01.bin is missing AND could not "
                   + "be rebuilt by Tools/capture/mkframes.py from the committed "
                   + "windows-run/01-baseline.pcapng -- this used to print SKIP")
    }
}

// --------------------------------------------------------------------------
// Device state, and the banner that depends on it.
//
// The app tells a person their mouse is latched in the bootloader. Getting
// that wrong in either direction is bad: a false negative leaves them with a
// dead mouse and no explanation, and a false positive tells them a working
// mouse is broken. So both directions are checked, and the "absent" case is
// driven against the real tool -- which is exactly the case this machine can
// produce, because there is no mouse attached to a test run.
// --------------------------------------------------------------------------
do {
    // The literal row format is egg-config's cmdDevices printf. If that
    // changes, these strings stop matching what the tool emits -- which is
    // why the `absent` case below runs the real binary as an anchor.
    let bootRow = "0x1977   0xff00     0x01    0x0006    Endgame Gear   "
                + "Bootloader (BOOTLOADER)"
    let appRow  = "0x1970   0xff00     0x01    0x0110    Endgame Gear   "
                + "OP1 8k v2 (application)"
    let header  = "PID      usagepage  usage   version   manufacturer   product"

    equal(Commands.parseDeviceState(header + "\n" + bootRow), .bootloader,
          "a 0x1977 row means the mouse is latched in the bootloader")
    equal(Commands.parseDeviceState(header + "\n" + appRow), .application,
          "an application row is not a bootloader")
    equal(Commands.parseDeviceState(header + "\n" + appRow + "\n" + bootRow),
          .unclear,
          "two mice in two modes is not a state the banner may guess at")
    equal(Commands.parseDeviceState("no VID 0x3367 device attached."), .absent,
          "no mouse is absent, not bootloader")
    equal(Commands.parseDeviceState(""), .absent,
          "empty output must not read as bootloader -- a false alarm tells "
          + "someone their working mouse is broken")
    equal(Commands.parseDeviceState(header), .absent,
          "a header with no rows is absent")

    // Against the real tool. With no mouse attached this is `absent`; with one
    // attached it is whatever the mouse is, and either way it must not crash
    // or read as `unclear`, which would mean the parser and the printf have
    // drifted apart.
    // The firmware version column. Real rows, in the tool's own layout --
    // `manufacturer` contains a space, which is exactly the thing a naive
    // column split gets wrong.
    let appV  = "0x1970   0xff00     0x01    0x0110    1.10      "
              + "Endgame Gear   OP1 8k v2 (application)"
    let bootV = "0x1977   0xff00     0x01    0x0006    0.06      "
              + "Endgame Gear   Bootloader (BOOTLOADER)"
    let badV  = "0x1970   0xff00     0x01    0x01a0    not BCD   "
              + "Endgame Gear   OP1 8k v2 (application)"
    equal(Commands.firmwareVersion(fromDevices: appV), "1.10",
          "the firmware column is read from the application row")
    equal(Commands.firmwareVersion(fromDevices: bootV), nil,
          "the bootloader's 0.06 is not offered as a firmware version")
    equal(Commands.firmwareVersion(fromDevices: badV), nil,
          "`not BCD` must not reach the UI as if it were a version")
    equal(Commands.firmwareVersion(fromDevices: bootV + "\n" + appV), "1.10",
          "with both modes present, the application row is the one that counts")
    equal(Commands.firmwareVersion(fromDevices: "no VID 0x3367 device attached."),
          nil, "no mouse means no version")
    equal(Commands.firmwareVersion(fromDevices: ""), nil,
          "empty output means no version")

    // AGAINST THE REAL TOOL, and this used to prove nothing when no mouse was
    // attached (fixed 2026-09-07). The check was `st != .unclear`, which
    // `.absent` satisfies -- so on a machine with no mouse, the assertion
    // passed while its own failure message claimed to be watching for a change
    // to `cmdDevices`' table printf, which `.absent` cannot see. A live anchor
    // that degrades to a tautology in the common case is worse than no anchor:
    // it reads as coverage.
    //
    // Both branches assert something now, and which branch runs is printed, so
    // a reader can tell which printf was actually exercised.
    let (drc, dtext) = run("egg-config", Commands.listDevices())
    if drc == -1 {
        print("SKIP: build/egg-config not present; device state parsed offline only")
    } else {
        check(!Commands.listDevices().contains("--yes"),
              "enumerating must never carry --yes")

        let st = Commands.parseDeviceState(dtext)
        let rows = dtext.split(separator: "\n").filter { $0.hasPrefix("0x") }
        if rows.isEmpty {
            // No mouse. The only printf reachable here is the empty-case one,
            // and `.absent` is the ONLY honest answer -- not merely "not
            // unclear". If cmdDevices ever changes that sentence, this fails.
            print("  (no device attached: checking the empty-case printf)")
            check(st == .absent,
                  "with no rows printed, parseDeviceState must say .absent, not "
                  + "\(st) -- cmdDevices' no-device sentence has probably changed",
                  dtext.prefix(300).description)
            check(dtext.contains("no VID 0x3367 device attached"),
                  "and that sentence is the one parseDeviceState keys on",
                  dtext.prefix(300).description)
        } else {
            // A mouse IS attached, so the table printf is exercised for real.
            print("  (device attached: checking the table printf, \(rows.count) rows)")
            check(st == .application || st == .bootloader,
                  "rows were printed but parseDeviceState said \(st) -- the "
                  + "printf in cmdDevices has probably changed",
                  dtext.prefix(300).description)
            // Every row must carry a mode tag, or the parser is reading a
            // table shape it does not understand and got its answer by luck.
            let tagged = rows.filter {
                $0.contains("(application)") || $0.contains("(BOOTLOADER)")
            }
            check(tagged.count == rows.count,
                  "\(rows.count - tagged.count) of \(rows.count) rows carry no "
                  + "mode tag; parseDeviceState reads those tags and nothing else",
                  dtext.prefix(300).description)
            if st == .application {
                check(Commands.firmwareVersion(fromDevices: dtext) != nil,
                      "an application row is present but no firmware version "
                      + "could be read out of it -- the version column moved",
                      dtext.prefix(300).description)
            }
        }
    }
}

// --------------------------------------------------------------------------
// The offline / read-only verbs the Advanced screen exposes
// --------------------------------------------------------------------------
// The organising claim of that screen is "nothing here writes". These check it
// as an argument-list property -- no --yes anywhere -- and then check each verb
// against the real CLI, because a builder that produced a refusal would be a
// button that cannot work.
do {
    equal(Commands.diffRecords("a.bin", "b.bin"), ["diff", "a.bin", "b.bin"],
          "diff takes two files")
    equal(Commands.frames(), ["frames"], "frames takes nothing")
    equal(Commands.dryRunField(record: "r.bin", field: "polling", value: "1000"),
          ["dryrun", "r.bin", "polling", "1000"],
          "the offline dryrun is the 4-argument form")
    equal(Commands.encodeField(field: "lod", value: "5",
                               from: "in.bin", to: "out.bin"),
          ["encode", "lod", "5", "in.bin", "out.bin"], "encode is field-first")
    equal(Commands.previewHandedness("left", from: "r.bin"),
          ["handedness", "left", "--from", "r.bin"],
          "handedness --from decides offline")
    equal(Commands.checkBootloader(), ["read-firmware", "--check"],
          "--check reports and stops")
    equal(Commands.verbose(["frames"]), ["-v", "frames"], "-v goes in front")
    equal(Commands.mapMachine(), ["map", "--machine"],
          "the machine listing is an argument list like any other")

    // THE OTHER THREE OFFLINE RUN VERBS (2026-09-07).
    equal(Commands.previewMap(button: "middle", action: "browser", from: "r.bin"),
          ["map", "middle", "browser", "--from", "r.bin"],
          "map --from decides offline")
    equal(Commands.previewCpi(stage: 3, x: 1200, y: 2400, from: "r.bin"),
          ["cpi", "3", "1200", "2400", "--from", "r.bin"],
          "cpi --from carries Y when Y differs")
    equal(Commands.previewCpi(stage: 1, x: 800, y: nil, from: "r.bin"),
          ["cpi", "1", "800", "--from", "r.bin"],
          "and omits it when it does not")
    equal(Commands.previewMulticlick(button: "left", mode: "off", value: 12,
                                     from: "r.bin"),
          ["multiclick", "left", "off", "12", "--from", "r.bin"],
          "multiclick --from carries its filter value")
    equal(Commands.previewMulticlick(button: "left", mode: "gx-safe", value: 12,
                                     from: "r.bin"),
          ["multiclick", "left", "gx-safe", "--from", "r.bin"],
          "a GX mode still drops the value the CLI would ignore")

    // EACH OFFLINE FORM IS ITS DEVICE FORM PLUS EXACTLY `--from PATH`.
    //
    // This is the property that matters, and it is stronger than the four
    // equalities above: those pin today's spelling, this pins the RELATIONSHIP.
    // A preview whose argv had drifted from the command it claims to preview
    // would be worse than no preview -- it would be a confident description of
    // something else. Asserted rather than left to the composition in
    // Commands.swift, so inlining that composition cannot silently break it.
    let pairs: [(String, [String], [String])] = [
        ("map", Commands.previewMap(button: "middle", action: "back"),
                Commands.previewMap(button: "middle", action: "back", from: "R")),
        ("cpi", Commands.previewCpi(stage: 2, x: 400, y: 800),
                Commands.previewCpi(stage: 2, x: 400, y: 800, from: "R")),
        ("multiclick",
                Commands.previewMulticlick(button: "right", mode: "off", value: 3),
                Commands.previewMulticlick(button: "right", mode: "off", value: 3,
                                           from: "R")),
        ("handedness", Commands.previewHandedness("right"),
                       Commands.previewHandedness("right", from: "R")),
    ]
    for (name, device, offline) in pairs {
        equal(offline, device + ["--from", "R"],
              "\(name) --from is the device form plus --from, nothing else")
    }

    let readOnly: [[String]] = [
        Commands.diffRecords("a", "b"), Commands.frames(),
        Commands.dryRunField(record: "r", field: "f", value: "v"),
        Commands.encodeField(field: "f", value: "v", from: "i", to: "o"),
        Commands.previewHandedness("left", from: "r"),
        Commands.previewMap(button: "middle", action: "back", from: "r"),
        Commands.previewCpi(stage: 1, x: 800, y: nil, from: "r"),
        Commands.previewMulticlick(button: "left", mode: "off", value: 8,
                                   from: "r"),
        Commands.mapMachine(),
        Commands.checkBootloader(), Commands.listDevices(),
        Commands.deviceInfo(), Commands.showSettings(from: "r"),
    ]
    check(readOnly.allSatisfy { !$0.contains("--yes") },
          "no verb on the Advanced screen carries --yes")
    check(readOnly.allSatisfy { !$0.contains("--confirm") },
          "no verb on the Advanced screen carries an approval token")

    // Against the real tools. `frames` and `--check` are the two that can be
    // run here with no arguments and no device.
    let (fs, fsText) = run("egg-config", Commands.frames())
    if fs == -1 {
        print("SKIP: build/egg-config not present; frames not driven")
    } else {
        check(fs == 0, "egg-config frames exits 0 with no device attached", fsText)
        check(fsText.contains("a1") || fsText.contains("a0"),
              "frames prints frames", String(fsText.prefix(200)))
    }

    // `diff` on two identical files must succeed and say nothing differs --
    // this is the command the firmware screen's success text points people at.
    let root = repoRoot()
    let cap = frameFixture()
    let t1 = FileManager.default.temporaryDirectory
                 .appendingPathComponent("egg-diff-a-\(getpid()).bin")
    let t2 = FileManager.default.temporaryDirectory
                 .appendingPathComponent("egg-diff-b-\(getpid()).bin")
    if let d = try? Data(contentsOf: cap), d.count == 1040 {
        try? (d + Data([0])).write(to: t1)
        try? (d + Data([0])).write(to: t2)
        defer { try? FileManager.default.removeItem(at: t1)
                try? FileManager.default.removeItem(at: t2) }
        let (ds, dText) = run("egg-config", Commands.diffRecords(t1.path, t2.path))
        if ds != -1 {
            check(ds == 0, "diff of a record against itself exits 0", dText)
            check(dText.contains("0 of 1024") || dText.contains("0 differ")
                  || dText.contains(" 0 "),
                  "diff of a record against itself reports no differences",
                  String(dText.suffix(200)))
        }
    }
}

// --------------------------------------------------------------------------
// `show --machine` -- the decoded-settings pane's only input
// --------------------------------------------------------------------------
// Driven against the REAL CLI, decoding the vendor's own captured record, for
// the same reason loadButtons is: a fixture would let the app and the tool
// drift together, and this parser is what decides whether the Settings screen
// tells a user the truth about their mouse.
do {
    // Rebuild the factory-default record from the capture, exactly as
    // Tests/test_show.sh does. 1040 bytes as Windows saw it, plus the trailing
    // pad a 1041-byte record carries.
    let root = repoRoot()
    let src  = frameFixture()
    let tmp  = FileManager.default.temporaryDirectory
                   .appendingPathComponent("egg-show-\(getpid()).bin")
    if let d = try? Data(contentsOf: src), d.count == 1040 {
        try? (d + Data([0])).write(to: tmp)
    }
    defer { try? FileManager.default.removeItem(at: tmp) }

    equal(Commands.showSettings(), ["show"], "show takes no flags by default")
    equal(Commands.showSettings(from: "/tmp/r.bin"), ["show", "--from", "/tmp/r.bin"],
          "--from names the file")
    equal(Commands.showSettingsMachine(from: "/tmp/r.bin"),
          ["show", "--from", "/tmp/r.bin", "--machine"],
          "the machine form is the human one plus --machine")
    check(!Commands.showSettingsMachine().contains("--yes"),
          "reading must never carry --yes")

    let (rc, text) = run("egg-config", Commands.showSettingsMachine(from: tmp.path))
    if rc == -1 || !FileManager.default.fileExists(atPath: tmp.path) {
        print("SKIP: build/egg-config or the capture is missing; show parsed offline only")
    } else {
        check(rc == 0, "egg-config show --machine --from <capture> exits 0", text)
        let c = Commands.parseShow(text)

        equal(c.fields.count, 13, "one row per settable field")
        equal(c.cpi.count, 4, "four CPI stages")
        equal(c.buttons.count, 8, "all eight button entries, not the six offered")
        equal(c.clicks.count, 5, "five multiclick sliders")
        equal(c.handed, "right", "the factory record is right-handed")

        // Against the screenshots (notes/config-wire-observed.md §7), so the
        // PARSER is pinned to the same anchor the decoder is -- a parser that
        // silently mapped a value to the wrong field would pass a shape check.
        func field(_ n: String) -> Commands.Shown.FieldValue? {
            c.fields.first { $0.name == n }
        }
        check(field("polling")?.text == "8000 Hz",
              "polling parses as 8000 Hz (basic.png)", field("polling")?.text ?? "nil")
        check(field("lod")?.text.contains("1.0mm") == true,
              "lod parses as 1.0mm (basic.png)", field("lod")?.text ?? "nil")
        check(field("led-on-liftoff")?.text.contains("stays lit") == true,
              "the inverted LED checkbox is not flipped by the parser")
        check(c.fields.allSatisfy { $0.ok },
              "every factory byte decodes; none is flagged undecodable")
        check(c.fields.allSatisfy { !$0.location.isEmpty },
              "every field carries the record offset it came from")
        check(c.fields.allSatisfy { $0.gate.isEmpty },
              "no field is gated on this record (0x6f is 0)")
        equal(c.raw["glass-mode"], "00", "glass-mode is off, so `lod` is the 11-step scale")

        // ------------------------------------------------------------------
        // settableValue: the number the Settings screen puts in a control
        // ------------------------------------------------------------------
        // THE PROPERTY THAT MATTERS, and it is not "the parse looks sane".
        // Every control on the Settings screen is seeded from `show
        // --machine` and writes back through `set`, so the value read out of
        // a reading has to be the SAME value `set` would take -- and for the
        // remapped fields the byte and that value are different numbers
        // (`cpi-downshift` stores 2 for item 1). A GUI that fed the byte back
        // would move the setting while showing the user nothing changed.
        //
        // Proved end to end, offline: for each field, take what the app would
        // show, hand it to `egg-config dryrun` against the SAME record, and
        // require the tool to say the field's own byte did not move.
        //
        // ON THE TOOL'S OWN SENTENCE, not on a byte count. The first version
        // of this asserted "0 of 1024 payload bytes differ" and failed all
        // thirteen fields, which looked like a real defect for a few minutes.
        // It was not: `dryrun` zeroes record 0x01-0x04 as the vendor does, so
        // a no-op write still moves one POLICY byte and the count is never 0.
        // The CLI already distinguishes the two and prints "it already holds
        // the value asked for" when the field's own byte is absent from the
        // diff. That sentence is the property; the count is not.
        var roundTripped = 0
        for f in c.fields {
            let v = Commands.settableValue(of: f)
            check(!v.isEmpty, "settableValue found nothing for \(f.name)", f.text)
            let (drc, dOut) = run("egg-config",
                                  Commands.dryRunField(record: tmp.path,
                                                       field: f.name, value: v))
            if drc == -1 { continue }
            check(drc == 0,
                  "egg-config refused \(f.name)=\(v), the value the Settings "
                  + "screen would show for it", String(dOut.prefix(300)))
            check(dOut.contains("it already holds the value asked for"),
                  "\(f.name)=\(v) is legal but is NOT what the record holds; "
                  + "the control would silently move the setting",
                  String(dOut.split(separator: "\n")
                             .filter { $0.contains("wire 0x") }
                             .joined(separator: "; ")))
            roundTripped += 1
        }
        // Without this the loop above proves nothing when the CLI is missing.
        check(roundTripped >= 13,
              "only \(roundTripped) of 13 fields were round-tripped through "
              + "dryrun; the check above was mostly skipped")

        // The positive control. The same assertion has to FAIL on a value that
        // is legal but wrong, or it is measuring nothing (a passing check that
        // cannot fail is this repo's characteristic bug).
        do {
            let real = Commands.settableValue(
                of: c.fields.first { $0.name == "polling" }!)
            let other = real == "1000" ? "500" : "1000"
            let (orc, oOut) = run("egg-config",
                                  Commands.dryRunField(record: tmp.path,
                                                       field: "polling",
                                                       value: other))
            if orc != -1 {
                check(orc == 0, "the control value must itself be accepted")
                check(!oOut.contains("it already holds the value asked for"),
                      "polling=\(other) reported no change against a record "
                      + "holding \(real); the round-trip check above cannot "
                      + "fail and proves nothing")
            }
        }

        // And the accepts/reading agreement, which needs no CLI at all: a
        // value the screen would show must be one the field's own `accepts`
        // sentence allows.
        let (sc2, setText2) = run("egg-config", ["set"])
        if sc2 != -1 {
            for decl in Commands.parseFields(setText2).fields {
                guard let read = c.fields.first(where: { $0.name == decl.name })
                else { continue }
                let v = Commands.settableValue(of: read)
                if let choices = Commands.choices(for: decl.accepts) {
                    check(choices.contains(v),
                          "\(decl.name) reads as \(v), which is not one of the "
                          + "choices the picker would offer", decl.accepts)
                } else if let r = Commands.range(for: decl.accepts) {
                    check(Int(v).map { r.contains($0) } == true,
                          "\(decl.name) reads as \(v), outside the range the "
                          + "control would allow", decl.accepts)
                }
            }
        }

        let active = c.cpi.filter { $0.active }
        equal(active.count, 1, "exactly one CPI stage is active")
        equal(active.first?.index, 2, "CPI 2 is the selected radio (basic.png)")
        equal(c.cpi.map { $0.x }, [400, 800, 1600, 3200],
              "the four stages parse as 400/800/1600/3200 (CPI-levels.png)")
        check(c.cpi.allSatisfy { $0.x == $0.y }, "X equals Y on every stage")

        // `offered` is what the GUI keys its editability hint off, and it must
        // match the six rows the vendor's own page shows.
        equal(c.buttons.filter { $0.offered }.count, 6,
              "six of eight entries are the ones `map` will touch (§7.15)")
        check(c.buttons.allSatisfy { !$0.unknown },
              "no factory entry decodes to UNKNOWN")
        check(c.buttons.first { $0.slot == "wheel-down" }?.action == "scroll-down",
              "wheel-down parses as scroll-down (button-mapping.png)")
        check(c.buttons.allSatisfy { $0.raw.split(separator: " ").count == 7 },
              "every binding carries all seven of its raw bytes")
        check(c.clicks.allSatisfy { $0.mode == "off" && $0.value == 8 },
              "all five sliders parse as filter 8 (buttons.png)")
    }

    // FALSIFICATION (§6.2). The parser must reject rows it cannot trust rather
    // than half-filling a struct: a FIELD row with a missing column, or a CPI
    // row whose numbers are not numbers, would otherwise reach the pane as a
    // confident-looking zero.
    let junk = """
    FIELD\tpolling\t0x05 = 01\t8000 Hz
    FIELD\t\t\t\t\t
    CPI\t1\tnot-a-number\t400\t0\t1
    BUTTON\tright\tright-click
    MULTICLICK\tleft
    HANDED
    RAW\tglass-mode\t6f
    NONSENSE\ta\tb\tc
    """
    let j = Commands.parseShow(junk)
    equal(j.cpi.count, 0, "a CPI row with a non-numeric field is dropped, not zeroed")
    equal(j.buttons.count, 0, "a short BUTTON row is dropped")
    equal(j.clicks.count, 0, "a short MULTICLICK row is dropped")
    equal(j.handed, "", "a HANDED row with no value leaves handedness unset")
    equal(j.raw.count, 0, "a short RAW row is dropped")
    equal(j.fields.count, 0,
          "a FIELD row with the wrong column count is dropped, and so is one "
          + "whose name is empty -- the pane keys its rows on the name")
    check(j.isEmpty, "nothing in that block was usable, and isEmpty says so")
    check(Commands.parseShow("").isEmpty, "empty input parses to an empty result")
    check(Commands.parseShow("total nonsense\nwith no tabs").isEmpty,
          "prose parses to nothing rather than to a guess")
}

// --------------------------------------------------------------------------
// `map --machine` -- the button and action tables, ONE parser
// --------------------------------------------------------------------------
// This loop lived inside ConfigModel.loadButtons until 2026-09-07, where no
// test could reach it and where the Advanced screen could not reuse it. A
// second copy would have been free to disagree with the first about which
// buttons the CLI accepts, and a GUI offering a choice the tool refuses is the
// exact defect an audit found on 2026-09-06 -- five of twenty-four menu entries
// were guaranteed refusals because the app was scraping prose.
do {
    let good = [
        "BUTTON\tleft\t0",
        "BUTTON\tright\t1",
        "BUTTON\tmiddle\t1",
        "ACTION\tback\tmouse\tnone",
        "ACTION\tfixed-cpi\tcpi\tcpi",
        "ACTION\tkey\tkeyboard\tkey",
        "KEY\ta",
        "KEY\tkp-plus",
    ].joined(separator: "\n")
    let t = Commands.parseButtonTables(good)
    equal(t.buttons, ["right", "middle"],
          "a button the vendor's page does not offer (flag 0) is dropped -- "
          + "the CLI refuses `left` and the picker must not present it")
    equal(t.actions, ["back", "fixed-cpi", "key"], "every ACTION row is kept")
    equal(t.actionArg["back"], "none", "an action that takes no argument says so")
    equal(t.actionArg["fixed-cpi"], "cpi", "and one that takes a CPI says that")
    equal(t.actionArg["key"], "key", "and one that takes a key says that")
    equal(t.keyNames, ["a", "kp-plus"], "KEY rows are the parser's own table")

    // FALSIFICATION (§6.2). Rows with the wrong column count must be DROPPED,
    // not half-read: a BUTTON row with no flag column would otherwise decide
    // whether to offer a button by reading a field that is not there.
    let junk = [
        "BUTTON\tright",
        "BUTTON\tmiddle\t1\textra",
        "ACTION\tback\tmouse",
        "KEY",
    ].joined(separator: "\n")
    let j = Commands.parseButtonTables(junk)
    equal(j.buttons.count, 0, "a BUTTON row without its flag column is dropped")
    equal(j.actions.count, 0, "a short ACTION row is dropped")
    equal(j.keyNames.count, 0, "a bare KEY row is dropped")
    check(Commands.parseButtonTables("").buttons.isEmpty,
          "empty input parses to empty tables")

    // The strings the prose scraper actually offered as bindable actions.
    // Pinned by name, because "it does not parse prose" is the claim and these
    // are what proved it once did.
    let prose = [
        "Actions:",
        "  mouse:",
        "    left-click     00 01",
        "    fixed-cpi      takes a CPI value 10-30000 in steps of 10,",
        "                   e.g. fixed-cpi:1600, or fixed-cpi:1600x800",
        "Keys: a-z, 0-9, f1-f12 and kp0-kp9 are parsed as ranges. The rest are",
        "named, and this is ALL of them, printed from the same table --machine",
    ].joined(separator: "\n")
    let pt = Commands.parseButtonTables(prose)
    check(pt.buttons.isEmpty && pt.actions.isEmpty && pt.keyNames.isEmpty,
          "the HUMAN listing parses to nothing at all -- `e.g.`, `for` and "
          + "`minus` can no longer reach a picker")

    // Against the real tool, because a fixture agreeing with itself proves
    // nothing about the format the CLI actually prints.
    let (rc, text) = run("egg-config", Commands.mapMachine())
    if rc == -1 {
        print("SKIP: build/egg-config not present; --machine parsed offline only")
    } else {
        check(rc == 0, "egg-config map --machine exits 0 with no device", text)
        let real = Commands.parseButtonTables(text)
        check(real.buttons.count >= 4,
              "the real listing yields rebindable buttons",
              real.buttons.joined(separator: ","))
        check(!real.buttons.contains("left") && !real.buttons.contains("cpi-button"),
              "and never the two the CLI refuses",
              real.buttons.joined(separator: ","))
        equal(real.actions.count, 19,
              "all nineteen actions parse -- the set 7.17.1 proved closed")
        check(real.actionArg["fixed-cpi"] == "cpi" && real.actionArg["key"] == "key",
              "the two actions that take an argument are both flagged as such")
        check(real.keyNames.count > 20,
              "and the key table is populated",
              String(real.keyNames.count))
    }
}

// --------------------------------------------------------------------------
print("\(checks - failures)/\(checks) checks passed")
if failures > 0 { exit(1) }
