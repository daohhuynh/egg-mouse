// EGGApp.swift -- the macOS front end for egg-config and egg-flash.
//
// WHAT THIS APP IS ALLOWED TO DO, in one place, because it is the whole safety
// argument for having a GUI at all:
//
//   - It builds command lines. It does not talk to the mouse.
//   - It never kills a running flash.
//   - It shows a dry run before anything is written, and for a flash it makes
//     the user carry the --confirm token across from that dry run (§4.2c: an
//     approval must be bound to the exact bytes it approves, and a button you
//     click without reading is a reflex, not a decision).
//
// WHAT IT DOES NOT DO, corrected 2026-09-07 because this block used to claim it.
// **It does not refuse to close its own window during a write phase.** There is
// no NSWindowDelegate and no applicationShouldTerminate anywhere in this target,
// and the comment 20 lines below has always said so -- the header and the code
// disagreed, in the paragraph the file calls "the whole safety argument".
//
// The claim was not merely false, it was the wrong argument. Closing this window
// does not endanger a flash, because CLAUDE.md §3 puts the write phase in
// ANOTHER PROCESS. `egg-flash` is spawned, not embedded: when this app exits the
// child is reparented and keeps running, and NoQuitDuringWrite ignores SIGINT,
// SIGTERM, SIGHUP *and SIGPIPE* from erase to verified image -- SIGPIPE being
// exactly the signal a dying parent delivers when the pipes close. So the flash
// finishes whether this window is open or not; what is lost by quitting is the
// user's view of it, which is why the state is made loud instead of blocked.
//
// A quit blocker would also be ceremony: Cmd-Q and the Dock item can be
// intercepted, force-quit cannot, and none of the three can stop the child. It
// would add untestable GUI code for a property already guaranteed by the
// process boundary (§4.4: if a step cannot prove something the next step depends
// on, cut it rather than pay for it).
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
            // The Dock quit item and Cmd-Q cannot be removed, so the app makes
            // the state loud instead: FirmwareView shows a full-window banner
            // and the window title changes while a write is in flight. See the
            // header for why loud is the right answer rather than blocked --
            // the write phase is in a child process that outlives this one.
            CommandGroup(replacing: .newItem) { }
        }
    }
}

enum Screen: Hashable {
    case home, config, firmware, updates, advanced
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
            case .advanced: AdvancedView(screen: $screen)
            }
            if let cmd = runner.running { RunningBar(command: cmd) }
        }
    }
}

/// The command line currently executing, across the bottom of every screen.
///
/// `ToolRunner.running` held exactly this string from the day it was written
/// and NOTHING read it (found 2026-09-07, by the same sweep that found
/// `FirmwareModel.busy` written five times and read nowhere). Two ways to fix a
/// property nobody reads: delete it, or show it. Showing it is right here --
/// every screen's own output pane only fills in once the tool EXITS, so until
/// then a long command like `read-firmware`'s 65-block read looked to the user
/// exactly like a hung app.
///
/// It prints the argument list verbatim, including `-v` when the Advanced
/// screen's switch is on, because "what is this app doing to my mouse" should
/// be answerable without trusting a summary of it.
struct RunningBar: View {
    let command: String
    var body: some View {
        HStack(spacing: 8) {
            ProgressView().controlSize(.small)
            Text(command)
                .font(.system(.caption, design: .monospaced))
                .lineLimit(1).truncationMode(.middle)
                .textSelection(.enabled)
            Spacer()
        }
        .padding(.horizontal, 12).padding(.vertical, 6)
        .frame(maxWidth: .infinity)
        .background(Color.secondary.opacity(0.12))
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
