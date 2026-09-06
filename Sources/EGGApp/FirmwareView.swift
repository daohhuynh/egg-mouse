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
                        step1
                        Divider()
                        step2
                        Divider()
                        step3
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
            Text("This puts the mouse into its bootloader, where it stops "
                 + "behaving as a mouse until a firmware write completes. Have "
                 + "another pointing device to hand.")
                .font(.caption).foregroundStyle(.orange)
                .fixedSize(horizontal: false, vertical: true)
            HStack {
                TextField("Where to save", text: $m.backupPath)
                Button("Choose…") { chooseSave() }
            }
            Button("Back up firmware") {
                run(Commands.backupFirmware(to: effectiveBackup))
            }
            .disabled(runner.writePhaseInProgress)
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
                .disabled(m.updaterPath.isEmpty)
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
                .disabled(m.updaterPath.isEmpty || runner.writePhaseInProgress)

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

    /// Nil when a flash is allowed; otherwise the reason, shown to the user.
    /// One function decides this and Tests/test_app_commands.swift drives it.
    private var blocked: String? {
        Commands.flashBlockedReason(
            updater: m.updaterPath,
            backup: effectiveBackup,
            backupExists: FileManager.default.fileExists(atPath: effectiveBackup),
            token: m.typedToken,
            versionIsProven: m.selectedIsProven,
            acknowledgedUntested: m.acknowledgeUntested,
            writeInProgress: runner.writePhaseInProgress)
    }

    private var canFlash: Bool { blocked == nil }

    private var effectiveBackup: String {
        m.backupPath.isEmpty ? defaultBackup() : m.backupPath
    }


    private func startFlash() {
        guard blocked == nil else { return }
        let args = Commands.flash(updater: m.updaterPath,
                                  backup: effectiveBackup,
                                  token: m.typedToken,
                                  version: m.version,
                                  versionIsProven: m.selectedIsProven)
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
                                          version: m.version))
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
}
