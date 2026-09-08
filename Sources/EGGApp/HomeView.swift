// HomeView.swift -- the root screen.
//
// Three destinations, ordered by how much damage each can do, and each says so
// before it is opened rather than after. "Friendly for any user" does not mean
// hiding that one of these buttons erases the mouse's firmware.
import SwiftUI

struct HomeView: View {

    /// Exactly the marker `UpdatesView.repoRoot()` looks for. Kept beside the
    /// card that depends on it: a card enabled by one rule and a screen gated by
    /// another is how "it does nothing when I click it" happens.
    private var repoAvailable: Bool {
        var d = Bundle.main.bundleURL.deletingLastPathComponent()
        for _ in 0..<6 {
            if FileManager.default.fileExists(
                    atPath: d.appendingPathComponent("Tools/pe/ingest.py").path) {
                return true
            }
            d = d.deletingLastPathComponent()
        }
        let cwd = URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
        return FileManager.default.fileExists(
            atPath: cwd.appendingPathComponent("Tools/pe/ingest.py").path)
    }
    @Binding var screen: Screen
    @EnvironmentObject var runner: ToolRunner
    @State private var toolsFound = ToolRunner.locate("egg-config") != nil
                                 && ToolRunner.locate("egg-flash") != nil
    @State private var deviceState: Commands.DeviceState = .absent
    /// The firmware the attached mouse is running, from `devices`. Nil when
    /// there is no mouse, when it is in the bootloader, or when bcdDevice is
    /// not something the vendor's own decode reads as a version.
    @State private var firmware: String?

    /// Enumerate on appear. `devices` opens no handle and sends no frame, so
    /// this costs the mouse nothing -- and it is the only way the app can tell
    /// a person that their mouse is latched in the bootloader at the moment
    /// they are wondering why it stopped working.
    private func refreshDeviceState() async {
        guard let r = try? await runner.run("egg-config", Commands.listDevices())
        else { return }
        deviceState = Commands.parseDeviceState(r.text)
        firmware = Commands.firmwareVersion(fromDevices: r.text)
    }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                VStack(alignment: .leading, spacing: 4) {
                    Text("Endgame Gear OP1 8k v2").font(.largeTitle).bold()
                    Text("Settings and firmware, on macOS.")
                        .foregroundStyle(.secondary)
                    if let v = firmware {
                        Text("Attached, running firmware \(v).")
                            .font(.callout).foregroundStyle(.secondary)
                    } else if deviceState == .absent {
                        Text("No mouse attached.")
                            .font(.callout).foregroundStyle(.secondary)
                    }
                }

                if deviceState == .bootloader {
                    BootloaderBanner(screen: $screen)
                }

                if !toolsFound {
                    Label(
                        "egg-config and egg-flash were not found next to this "
                        + "app. Build them with `cmake -S . -B build && cmake "
                        + "--build build`, then reopen this app from the same "
                        + "folder.",
                        systemImage: "wrench.and.screwdriver")
                        .padding(10)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .background(Color.orange.opacity(0.15))
                        .clipShape(RoundedRectangle(cornerRadius: 8))
                }

                HomeCard(
                    title: "Settings",
                    systemImage: "slider.horizontal.3",
                    tint: .accentColor,
                    blurb: "Polling rate, CPI, lift-off distance, button "
                         + "mapping. Every change is read back and verified, "
                         + "and the first good copy of your settings is saved "
                         + "automatically so there is always a way back.",
                    risk: "Reversible.",
                    action: { screen = .config })

                HomeCard(
                    title: "Firmware",
                    systemImage: "cpu",
                    tint: .red,
                    blurb: "Back up the firmware that is on the mouse now, or "
                         + "write a new one from Endgame's own updater. The "
                         + "backup is a separate step and is required before "
                         + "any write.",
                    risk: "A firmware write cannot be cancelled once it starts. "
                        + "There is one mouse and no spare.",
                    action: { screen = .firmware })

                // SAY SO WHEN IT CANNOT WORK, RATHER THAN AFTER THE CLICK.
                //
                // This screen shells out to the repository's own ingest
                // scripts, found by walking up from the bundle for
                // Tools/pe/ingest.py. A .dmg install puts the app in
                // /Applications with a working directory of "/", and the
                // Homebrew formula installs no part of Tools/, so for everyone
                // who did not clone the repo the marker is not there and the
                // screen can only print a refusal. Advertising it identically
                // in both cases sent a downloader through choosing a file and
                // typing a version label to be told to open the app from inside
                // a checkout they do not have. Found 2026-09-08.
                HomeCard(
                    title: "New versions",
                    systemImage: "shippingbox",
                    tint: .teal,
                    blurb: repoAvailable
                         ? "Point this at a firmware updater or configuration "
                         + "tool that this build has never seen. It reads the "
                         + "file and reports whether the settings layout and "
                         + "the firmware image can be identified, or refuses, "
                         + "and says exactly what it could not work out."
                         : "Needs the egg-mouse source checkout, because it "
                         + "runs the repository's own ingest scripts. It is not "
                         + "part of a downloaded or Homebrew install, so there "
                         + "is nothing for it to run from here.",
                    risk: repoAvailable
                        ? "Reads files only. Sends nothing to the mouse."
                        : "Unavailable in this install.",
                    action: { screen = .updates })
                    .disabled(!repoAvailable)

                HomeCard(
                    title: "Advanced",
                    systemImage: "wrench.and.screwdriver",
                    tint: .gray,
                    blurb: "Compare two saved records, preview a change with "
                         + "the mouse unplugged, list every USB interface, "
                         + "dump the exact command frames, and turn on the "
                         + "hex log. Nothing on this screen writes.",
                    risk: "Read-only. Most of it sends nothing at all.",
                    action: { screen = .advanced })

                Text("This app does not talk to the mouse itself. It runs "
                     + "egg-config and egg-flash and shows you what they say.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            .padding(24)
        }
        .task { await refreshDeviceState() }
    }
}

struct HomeCard: View {
    let title: String
    let systemImage: String
    let tint: Color
    let blurb: String
    let risk: String
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            HStack(alignment: .top, spacing: 14) {
                Image(systemName: systemImage)
                    .font(.system(size: 26))
                    .frame(width: 38)
                    .foregroundStyle(tint)
                VStack(alignment: .leading, spacing: 5) {
                    Text(title).font(.title3).bold()
                    Text(blurb).font(.callout).foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                    Text(risk).font(.caption).bold().foregroundStyle(tint)
                }
                Spacer()
                Image(systemName: "chevron.right").foregroundStyle(.tertiary)
            }
            .padding(16)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(Color(nsColor: .controlBackgroundColor))
            .clipShape(RoundedRectangle(cornerRadius: 10))
            .overlay(RoundedRectangle(cornerRadius: 10)
                        .strokeBorder(Color.secondary.opacity(0.25)))
        }
        .buttonStyle(.plain)
    }
}
