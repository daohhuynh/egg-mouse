// EGGApp.swift -- the macOS front end for egg-config and egg-flash.
//
// WHAT THIS APP IS ALLOWED TO DO, in one place, because it is the whole safety
// argument for having a GUI at all:
//
//   - It builds command lines. It does not talk to the mouse.
//   - It never kills a running flash, and it refuses to close its own window
//     while one is in the write phase (§4.2: after erase, quitting is the bug).
//   - It shows a dry run before anything is written, and for a flash it makes
//     the user carry the --confirm token across from that dry run (§4.2c: an
//     approval must be bound to the exact bytes it approves, and a button you
//     click without reading is a reflex, not a decision).
//
// CLAUDE.md §3 says a GUI must not own the flash write phase. It does not: the
// write phase lives inside `egg-flash`, a process that ignores SIGINT, SIGTERM
// and SIGHUP from erase to verified image, and this app is only its window.
import SwiftUI

@main
struct EGGApp: App {
    @StateObject private var runner = ToolRunner()
    @State private var screen: Screen = .home

    var body: some Scene {
        WindowGroup("EGG Mouse") {
            RootView(screen: $screen)
                .environmentObject(runner)
                .frame(minWidth: 780, minHeight: 560)
        }
        .windowResizability(.contentSize)
        .commands {
            // The Dock quit item and Cmd-Q are the GUI-specific hazard the CLI
            // does not have. Neither can be removed, so the app makes the state
            // loud instead: FirmwareView shows a full-window banner and the
            // window title changes while a write is in flight.
            CommandGroup(replacing: .newItem) { }
        }
    }
}

enum Screen: Hashable {
    case home, config, firmware, updates
}

struct RootView: View {
    @EnvironmentObject var runner: ToolRunner
    @Binding var screen: Screen

    var body: some View {
        VStack(spacing: 0) {
            if runner.writePhaseInProgress {
                WritePhaseBanner()
            }
            switch screen {
            case .home:     HomeView(screen: $screen)
            case .config:   ConfigView(screen: $screen)
            case .firmware: FirmwareView(screen: $screen)
            case .updates:  UpdatesView(screen: $screen)
            }
        }
    }
}

/// Shown across the top of every screen while a flash write phase is running.
struct WritePhaseBanner: View {
    var body: some View {
        HStack(spacing: 10) {
            Image(systemName: "exclamationmark.triangle.fill")
            VStack(alignment: .leading, spacing: 2) {
                Text("Writing firmware. Do not quit, unplug, or sleep this Mac.")
                    .font(.headline)
                Text("The mouse has no valid firmware until this finishes. "
                     + "If it fails, run it again — stopping is what makes it "
                     + "permanent.")
                    .font(.caption)
            }
            Spacer()
        }
        .padding(12)
        .frame(maxWidth: .infinity)
        .background(Color.red.opacity(0.18))
    }
}

/// THE RECOVERY BANNER. Shown whenever the mouse enumerates as its bootloader.
///
/// This is the state a user is most likely to reach and least likely to
/// understand: a firmware backup or a flash enters the bootloader with `A1 3A`,
/// and that entry LATCHES -- it survives unplugging and is cleared only by a
/// COMPLETED flash (notes/bootloader-observed.md §5a, §5b; CLAUDE.md §4.2b
/// calls the latched window the accepted cost of entering the vendor's way).
/// So "I cancelled the backup and now my mouse does not work" is an expected
/// outcome of using this app correctly, and the answer to it belonged in the
/// app rather than only in the README.
///
/// It says what is true and no more. It does not offer a one-click fix,
/// because the fix is a firmware write and §4.2c forbids an approval that is
/// not bound to bytes the person has been shown.
struct BootloaderBanner: View {
    @Binding var screen: Screen

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Label("Your mouse is in its bootloader", systemImage: "exclamationmark.triangle.fill")
                .font(.headline)
            Text("It shows up as ProductID 0x1977 and will not work as a mouse "
                 + "until a firmware write finishes. Nothing is broken and "
                 + "nothing has been lost.")
                .fixedSize(horizontal: false, vertical: true)
            Text("This is what an interrupted firmware backup or flash leaves "
                 + "behind. That entry is sticky on purpose: unplugging it will "
                 + "not clear it, and only a completed write will. If you got "
                 + "here by holding both mouse buttons while plugging in "
                 + "instead, that kind does clear on its own -- just replug "
                 + "without holding anything.")
                .font(.caption).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
            HStack {
                Button("Go to Firmware") { screen = .firmware }
                Text("Endgame's own Windows updater also accepts a mouse that "
                     + "is already in this state, if you would rather use it.")
                    .font(.caption).foregroundStyle(.secondary)
            }
        }
        .padding(12)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(Color.orange.opacity(0.18))
        .clipShape(RoundedRectangle(cornerRadius: 8))
    }
}

/// A back button plus a title, used by every non-home screen.
struct ScreenHeader: View {
    let title: String
    let subtitle: String
    @Binding var screen: Screen
    @EnvironmentObject var runner: ToolRunner

    var body: some View {
        HStack(alignment: .top) {
            Button {
                screen = .home
            } label: {
                Label("Home", systemImage: "chevron.left")
            }
            .disabled(runner.writePhaseInProgress)
            .help(runner.writePhaseInProgress
                  ? "Not while firmware is being written."
                  : "Back to the home screen")
            VStack(alignment: .leading, spacing: 2) {
                Text(title).font(.title2).bold()
                Text(subtitle).font(.caption).foregroundStyle(.secondary)
            }
            Spacer()
        }
        .padding([.horizontal, .top])
    }
}

/// Monospaced output pane. Everything a tool prints is shown verbatim: the
/// tools explain their own refusals carefully and paraphrasing them in the GUI
/// would be a second, untested explanation of the same safety rule.
struct OutputPane: View {
    let text: String
    var body: some View {
        ScrollView {
            Text(text.isEmpty ? "—" : text)
                .font(.system(.caption, design: .monospaced))
                .textSelection(.enabled)
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(8)
        }
        .background(Color(nsColor: .textBackgroundColor))
        .overlay(RoundedRectangle(cornerRadius: 6)
                    .strokeBorder(Color.secondary.opacity(0.3)))
        .clipShape(RoundedRectangle(cornerRadius: 6))
    }
}
