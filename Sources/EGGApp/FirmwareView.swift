// FirmwareView.swift -- firmware backup and flashing.
//
// THIS SCREEN IS THE REASON THE APP SHELLS OUT. Read CLAUDE.md §3 and §4.2
// before changing anything here.
//
//   §3: "A GUI DOES NOT INHERIT THIS. ... a window has a close button, a Dock
//   quit item and a force-quit, none of which a post-erase write phase may
//   honour. A GUI may drive config freely; it must not own the flash write
//   phase. Shell out to `egg-flash` and let it keep being a process that cannot
//   be asked to stop."
//
//   §4.2: "After erase: quitting is the bug. The device has no valid
//   application; exiting cleanly guarantees the bad outcome."
//
// So: there is no Cancel button on this screen, no timeout, and no code path
// that sends a signal to egg-flash. The subprocess ignores SIGINT, SIGTERM and
// SIGHUP for the erase-through-verified-image window on its own; this app's
// contribution is simply to never try.
//
// The order of the three steps is §4.4's rule made visible: "Order new work so
// each step removes untested code from the next one, and make the cheapest
// irreversible step come last."
import SwiftUI
import UniformTypeIdentifiers

@MainActor
final class FirmwareModel: ObservableObject {
    @Published var updaterPath: String = ""
    @Published var backupPath: String = ""
    @Published var version: String = ""
    @Published var versions: [String] = []
    @Published var provenLabel: String = ""
    @Published var acknowledgeUntested = false
    /// The mouse was put into update mode by holding LEFT+RIGHT while plugging
    /// in, rather than by this app. egg-flash cannot tell the two apart -- the
    /// USB identity is byte-identical -- so it refuses to write to a bootloader
    /// it did not enter itself unless told. Separate from acknowledgeUntested on
    /// purpose: they are different admissions (CLAUDE.md 4.2c's reasoning about
    /// approvals given by reflex).
    @Published var buttonEntered = false
    // --- restore: writing a saved backup BACK to the mouse ------------------
    @Published var restoreFrom: String = ""      // the image to write
    @Published var restoreCurrent: String = ""   // a fresh read of what is on it now
    @Published var restoreToken: String = ""
    @Published var restoreTyped: String = ""
    @Published var token: String = ""
    @Published var typedToken: String = ""
    @Published var output = ""
    @Published var busy = false

    /// Read the manifest out of the tool rather than duplicating it. The
    /// manifest is source (FirmwareManifest.cpp) and a copy here would be a
    /// second table to keep true.
    func loadVersions(_ runner: ToolRunner) async {
        guard let r = try? await runner.run("egg-flash", Commands.listVersions())
        else { return }
        let parsed = Commands.parseVersions(r.text)
        versions = parsed.labels
        provenLabel = parsed.proven ?? ""
        if version.isEmpty {
            version = parsed.proven ?? (parsed.labels.first ?? "")
        }
    }

    var selectedIsProven: Bool { version == provenLabel }
}

struct FirmwareView: View {
    @Binding var screen: Screen
    @EnvironmentObject var runner: ToolRunner
    @StateObject private var m = FirmwareModel()

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            ScreenHeader(title: "Firmware",
                         subtitle: "Back it up first. Then, only if you mean "
                                 + "to, write a new one.",
                         screen: $screen)

            HStack(alignment: .top, spacing: 16) {
                ScrollView {
                    VStack(alignment: .leading, spacing: 14) {
                        step0
                        Divider()
                        step1
                        Divider()
                        step2
                        Divider()
                        step3
                        Divider()
                        restoreSection
                    }
                    .padding(.trailing, 4)
                }
                .frame(width: 340)

                OutputPane(text: runner.liveOutput.isEmpty ? m.output
                                                           : runner.liveOutput)
            }
            .padding([.horizontal, .bottom])
        }
        .task { await m.loadVersions(runner) }
    }

    // ---------------------------------------------------------------- step 0
    //
    // ADDED 2026-09-06. Step 1 said "This puts the mouse into its bootloader",
    // which was never true: `read-firmware` REQUIRES the bootloader and cannot
    // reach it. The app had no way to send the entry command at all, so the only
    // route was the buttons -- unstated, and now the weaker of the two paths
    // because egg-flash records which one was used.
    private var step0: some View {
        VStack(alignment: .leading, spacing: 6) {
            Label("0 · Put the mouse into update mode", systemImage: "bolt")
                .font(.headline)
            Text("Sends one command and then only watches. Nothing is erased "
                 + "and nothing is written.")
                .font(.caption).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
            Text("Once this is done the mouse STOPS BEING A MOUSE until a "
                 + "firmware write finishes. Unplugging does not undo it. Have "
                 + "another pointing device to hand before you press this.")
                .font(.caption).foregroundStyle(.orange)
                .fixedSize(horizontal: false, vertical: true)
            HStack {
                Button("Put the mouse into update mode") {
                    run(Commands.enterBootloader())
                }
                .disabled(runner.writePhaseInProgress || m.busy)
                Button("Try to leave update mode") {
                    run(Commands.leaveBootloader())
                }
                .disabled(runner.writePhaseInProgress || m.busy)
            }
            Text("Leaving works if the mouse has firmware to go back to. If it "
                 + "was already mid-update it will come straight back into "
                 + "update mode — the tool's output says which happened.")
                .font(.caption).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)

            Toggle("I used the buttons instead (held left+right while plugging in)",
                   isOn: $m.buttonEntered)
                .font(.caption)
            Text("Tick this only if you did. The app cannot detect it — the "
                 + "mouse reports the same identity either way — and it relaxes "
                 + "a check, so it is a separate box from the firmware-version "
                 + "one on purpose.")
                .font(.caption).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    // ---------------------------------------------------------------- step 1
    private var step1: some View {
        VStack(alignment: .leading, spacing: 6) {
            Label("1 · Save what is on the mouse now", systemImage: "arrow.down.doc")
                .font(.headline)
            Text("Reads all 65 blocks back and writes them to a file. "
                 + "Read-only, and you can stop it at any time. A flash will "
                 + "not start without one of these.")
                .font(.caption).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
            Text("Needs the mouse to already be in update mode — do step 0 "
                 + "first. This does not put it there and will refuse if it "
                 + "is not.")
                .font(.caption).foregroundStyle(.orange)
                .fixedSize(horizontal: false, vertical: true)
            HStack {
                TextField("Where to save", text: $m.backupPath)
                Button("Choose…") { chooseSave() }
                    .disabled(m.busy)
            }
            Button("Back up firmware") {
                run(Commands.backupFirmware(to: effectiveBackup))
            }
            .disabled(runner.writePhaseInProgress || m.busy)
        }
    }

    // ---------------------------------------------------------------- step 2
    private var step2: some View {
        VStack(alignment: .leading, spacing: 6) {
            Label("2 · Choose the firmware", systemImage: "doc.badge.gearshape")
                .font(.headline)
            Text("Endgame's own updater .exe. Nothing is extracted from it "
                 + "until its SHA-256 matches a release this build knows.")
                .font(.caption).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
            HStack {
                TextField("Updater .exe", text: $m.updaterPath)
                Button("Choose…") { chooseUpdater() }
                    .disabled(m.busy)
            }
            Picker("Version", selection: $m.version) {
                ForEach(m.versions, id: \.self) { Text($0).tag($0) }
            }
            if !m.selectedIsProven && !m.version.isEmpty {
                VStack(alignment: .leading, spacing: 4) {
                    Text("\(m.version) has never been written to this mouse.")
                        .font(.caption).bold().foregroundStyle(.orange)
                    Text("Everything known about this device came from "
                         + "\(m.provenLabel). How it reacts to \(m.version) "
                         + "has not been observed.")
                        .font(.caption).foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                    Toggle("I understand this version is untested",
                           isOn: $m.acknowledgeUntested)
                        .font(.caption)
                }
                .padding(8)
                .background(Color.orange.opacity(0.12))
                .clipShape(RoundedRectangle(cornerRadius: 6))
            }
            Button("Check the image") {
                run(Commands.checkImage(updater: m.updaterPath, version: m.version))
            }
                .disabled(m.updaterPath.isEmpty || m.busy)
        }
    }

    // ---------------------------------------------------------------- step 3
    private var step3: some View {
        VStack(alignment: .leading, spacing: 6) {
            Label("3 · Write it", systemImage: "flame")
                .font(.headline)
                .foregroundStyle(.red)
            Text("Preview prints every byte that would go to the mouse and "
                 + "ends with a confirmation code. Type that code below to "
                 + "enable the write. If anything about the plan changes, the "
                 + "code changes and the old one stops working.")
                .font(.caption).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)

            Button("Preview the exact bytes") { preview() }
                .disabled(m.updaterPath.isEmpty || runner.writePhaseInProgress
                          || m.busy)

            if !m.token.isEmpty {
                VStack(alignment: .leading, spacing: 3) {
                    Text("Confirmation code for this plan")
                        .font(.caption).foregroundStyle(.secondary)
                    Text(m.token)
                        .font(.system(.title3, design: .monospaced)).bold()
                        .textSelection(.enabled)
                    Text("Type it below. It is not filled in for you on "
                         + "purpose — an approval you did not have to read is "
                         + "not an approval.")
                        .font(.caption).foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                }
                .padding(8)
                .background(Color.secondary.opacity(0.1))
                .clipShape(RoundedRectangle(cornerRadius: 6))
            }

            TextField("Confirmation code from the preview", text: $m.typedToken)
                .font(.system(.body, design: .monospaced))

            Button(role: .destructive) {
                startFlash()
            } label: {
                Text("Write firmware to the mouse").frame(maxWidth: .infinity)
            }
            .disabled(!canFlash)
            .help(blocked ?? "This cannot be stopped once it begins.")

            Text(blocked ?? "Once this starts there is no cancel, and closing "
                          + "the window will not stop it.")
                .font(.caption)
                .foregroundStyle(canFlash ? .red : .secondary)
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    // ------------------------------------------------------------- restore
    //
    // ADDED 2026-09-06. `restore-firmware` existed only on the command line,
    // which made the app's own promise -- "a saved image is what makes a failed
    // flash recoverable" (step 1's text) -- something the app could not deliver.
    //
    // Two files, and the distinction is the whole point: the one being WRITTEN
    // is an old backup, and --backup is a FRESH read of whatever is on the mouse
    // right now, which is the thing about to be erased. egg-flash refuses if
    // they are the same file, and refuses any image without the .origin sidecar
    // `read-firmware` writes beside it.
    private var restoreSection: some View {
        VStack(alignment: .leading, spacing: 6) {
            Label("Put a backup back", systemImage: "arrow.uturn.backward")
                .font(.headline)
            Text("Writes a firmware backup this app made back onto the mouse. "
                 + "This is the undo for a firmware write, not for settings — "
                 + "settings live on the Config screen.")
                .font(.caption).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
            Text("It only accepts backups this app made. A file without the "
                 + "matching .origin record beside it is refused, and so is one "
                 + "whose bytes changed since it was saved.")
                .font(.caption).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)

            HStack {
                TextField("Backup to write", text: $m.restoreFrom)
                Button("Choose…") { chooseOpen(into: { m.restoreFrom = $0 }) }
            }
            HStack {
                TextField("Fresh backup of what is on it NOW",
                          text: $m.restoreCurrent)
                Button("Choose…") { chooseOpen(into: { m.restoreCurrent = $0 }) }
            }
            Text("The second one is what is on the mouse at this moment — take "
                 + "it with step 1 first. It must be a different file.")
                .font(.caption).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)

            Button("Preview the restore") { previewRestore() }
                .disabled(m.restoreFrom.isEmpty || m.restoreCurrent.isEmpty
                          || runner.writePhaseInProgress || m.busy)

            if !m.restoreToken.isEmpty {
                Text("Confirmation code: \(m.restoreToken)")
                    .font(.system(.body, design: .monospaced)).bold()
                    .textSelection(.enabled)
            }
            TextField("Confirmation code from the preview", text: $m.restoreTyped)
                .font(.system(.body, design: .monospaced))

            Button(role: .destructive) { startRestore() } label: {
                Text("Write the backup back").frame(maxWidth: .infinity)
            }
            .disabled(restoreBlocked != nil)
            .help(restoreBlocked ?? "This cannot be stopped once it begins.")

            Text(restoreBlocked ?? "Same write phase as a flash: once it starts "
                                 + "there is no cancel.")
                .font(.caption)
                .foregroundStyle(restoreBlocked == nil ? .red : .secondary)
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    /// Nil when a restore is allowed; otherwise why not. Mirrors `blocked`, and
    /// deliberately does not reuse it: a restore has no updater .exe and no
    /// firmware version, so sharing the function would mean passing empty
    /// strings through checks that exist for a different command.
    private var restoreBlocked: String? {
        if m.busy { return kSomethingRunning }
        if runner.writePhaseInProgress {
            return "A firmware write is already running. It cannot be "
                 + "interrupted, and starting a second one is not possible."
        }
        if m.restoreFrom.isEmpty { return "Choose the backup you want written." }
        if !FileManager.default.fileExists(atPath: m.restoreFrom) {
            return "That backup file does not exist."
        }
        if !FileManager.default.fileExists(
                atPath: m.restoreFrom + ".origin") {
            return "That file has no .origin record beside it, so this app "
                 + "cannot tell it is a backup it made. egg-flash will refuse "
                 + "it. Use a file saved by step 1."
        }
        if m.restoreCurrent.isEmpty {
            return "Take a fresh backup of what is on the mouse now (step 1) "
                 + "and choose it as the second file. That is what makes this "
                 + "restore itself undoable."
        }
        if m.restoreCurrent == m.restoreFrom {
            return "Those are the same file. The second one must be a fresh "
                 + "read of what is on the mouse right now."
        }
        if m.restoreTyped.trimmingCharacters(in: .whitespaces).isEmpty {
            return "Run the preview and type the confirmation code it prints."
        }
        return nil
    }

    private func previewRestore() {
        Task {
            m.busy = true
            defer { m.busy = false }
            do {
                let r = try await runner.run(
                    "egg-flash",
                    Commands.requestRestoreToken(backup: m.restoreFrom,
                                                 current: m.restoreCurrent,
                                                 buttonEntered: m.buttonEntered))
                if let t = Commands.confirmToken(in: r.text) { m.restoreToken = t }
                m.output = r.text
            } catch { m.output = error.localizedDescription }
        }
    }

    private func startRestore() {
        guard restoreBlocked == nil else { return }
        let args = Commands.restoreFirmware(backup: m.restoreFrom,
                                            current: m.restoreCurrent,
                                            token: m.restoreTyped,
                                            buttonEntered: m.buttonEntered)
        Task {
            m.busy = true
            defer { m.busy = false }
            do {
                let r = try await runner.run("egg-flash", args, isWritePhase: true)
                m.output = r.text
            } catch { m.output = error.localizedDescription }
        }
    }

    /// Nil when a flash is allowed; otherwise the reason, shown to the user.
    /// One function decides this and Tests/test_app_commands.swift drives it.
    private var blocked: String? {
        if m.busy { return kSomethingRunning }
        return Commands.flashBlockedReason(
            updater: m.updaterPath,
            backup: effectiveBackup,
            backupExists: FileManager.default.fileExists(atPath: effectiveBackup),
            token: m.typedToken,
            versionIsProven: m.selectedIsProven,
            acknowledgedUntested: m.acknowledgeUntested,
            writeInProgress: runner.writePhaseInProgress)
    }

    private var canFlash: Bool { blocked == nil }

    /// THE RE-ENTRANCY GATE THIS SCREEN DID NOT HAVE (added 2026-09-07).
    ///
    /// `FirmwareModel.busy` was set true/false around all five operations and
    /// read by nothing -- no `.disabled`, no view, nothing. Every other screen
    /// guards itself with it (AdvancedView:64, ConfigView:298 and 363,
    /// UpdatesView:80); this one relied solely on `runner.writePhaseInProgress`,
    /// which is FALSE during bootloader entry, during `read-firmware`'s 65-block
    /// read, and during both previews. So a second press of "Back up firmware"
    /// launched a second egg-flash against the same bootloader, writing the same
    /// output path -- and `ToolRunner.run` clears `liveOutput` on every start,
    /// so the first run's output vanished while it was still going.
    ///
    /// It is a REASON rather than only a greyed button, because a control that
    /// goes dead without saying why is the same defect one step later.
    private let kSomethingRunning =
        "Something on this screen is already running. Wait for it to finish -- "
        + "a second egg-flash against the same device is not safe, and the two "
        + "would fight over the same output file."

    private var effectiveBackup: String {
        m.backupPath.isEmpty ? defaultBackup() : m.backupPath
    }


    private func startFlash() {
        guard blocked == nil else { return }
        let args = Commands.flash(updater: m.updaterPath,
                                  backup: effectiveBackup,
                                  token: m.typedToken,
                                  version: m.version,
                                  versionIsProven: m.selectedIsProven,
                                  buttonEntered: m.buttonEntered)
        Task {
            m.busy = true
            defer { m.busy = false }
            do {
                // isWritePhase is what raises the banner and disables every
                // navigation control in the app. There is no matching "stop".
                let r = try await runner.run("egg-flash", args, isWritePhase: true)
                m.output = r.text
            } catch {
                m.output = error.localizedDescription
            }
        }
    }

    /// Two commands, shown together: `dryrun` prints the byte stream, and
    /// `flash` with no --confirm refuses host-side and prints the code. The
    /// code appears nowhere else, and a Preview button that produced a byte
    /// dump but no code would leave a person stuck at the next field.
    private func preview() {
        Task {
            m.busy = true
            defer { m.busy = false }
            do {
                let d = try await runner.run(
                    "egg-flash",
                    Commands.dryRun(updater: m.updaterPath, version: m.version))
                let t = try await runner.run(
                    "egg-flash",
                    Commands.requestToken(updater: m.updaterPath,
                                          backup: effectiveBackup,
                                          version: m.version,
                                          buttonEntered: m.buttonEntered))
                if let tok = Commands.confirmToken(in: t.text) { m.token = tok }
                m.output = d.text + "\n\n" + t.text
            } catch {
                m.output = error.localizedDescription
            }
        }
    }

    private func run(_ args: [String]) {
        Task {
            m.busy = true
            defer { m.busy = false }
            do {
                let r = try await runner.run("egg-flash", args)
                m.output = "$ egg-flash " + args.joined(separator: " ")
                        + "\n\n" + r.text
                // Lift the confirmation code out of a dry run so a person can
                // see it -- but they still have to type it (§4.2c).
                if let t = Commands.confirmToken(in: r.text) { m.token = t }
            } catch {
                m.output = error.localizedDescription
            }
        }
    }

    private func defaultBackup() -> String {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)
            .first!.appendingPathComponent("egg-mouse-firmware-backup.bin").path
    }

    private func chooseUpdater() {
        let p = NSOpenPanel()
        p.canChooseFiles = true
        p.allowsMultipleSelection = false
        p.message = "Choose Endgame's firmware updater (.exe)"
        if p.runModal() == .OK, let u = p.url { m.updaterPath = u.path }
    }

    private func chooseSave() {
        let p = NSSavePanel()
        p.nameFieldStringValue = "egg-mouse-firmware-backup.bin"
        p.message = "Where to save the mouse's current firmware"
        if p.runModal() == .OK, let u = p.url { m.backupPath = u.path }
    }

    /// Pick an existing firmware backup. Takes a setter rather than a binding
    /// because the restore section has two of these fields and they must not be
    /// confusable: one is the image to write, the other is what is on the mouse
    /// now, and the whole guard depends on them being different files.
    private func chooseOpen(into set: @escaping (String) -> Void) {
        let p = NSOpenPanel()
        p.canChooseFiles = true
        p.canChooseDirectories = false
        p.allowsMultipleSelection = false
        p.message = "Choose a firmware backup saved by this app"
        if p.runModal() == .OK, let u = p.url { set(u.path) }
    }
}
