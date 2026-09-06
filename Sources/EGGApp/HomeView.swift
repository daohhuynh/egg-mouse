// HomeView.swift -- the root screen.
//
// Three destinations, ordered by how much damage each can do, and each says so
// before it is opened rather than after. "Friendly for any user" does not mean
// hiding that one of these buttons erases the mouse's firmware.
import SwiftUI

struct HomeView: View {
    @Binding var screen: Screen
    @EnvironmentObject var runner: ToolRunner
    @State private var toolsFound = ToolRunner.locate("egg-config") != nil
                                 && ToolRunner.locate("egg-flash") != nil

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                VStack(alignment: .leading, spacing: 4) {
                    Text("Endgame Gear OP1 8k v2").font(.largeTitle).bold()
                    Text("Settings and firmware, on macOS.")
                        .foregroundStyle(.secondary)
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

                HomeCard(
                    title: "New versions",
                    systemImage: "shippingbox",
                    tint: .teal,
                    blurb: "Point this at a firmware updater or configuration "
                         + "tool that this build has never seen. It reads the "
                         + "file and reports whether the settings layout and "
                         + "the firmware image can be identified — or refuses, "
                         + "and says exactly what it could not work out.",
                    risk: "Reads files only. Sends nothing to the mouse.",
                    action: { screen = .updates })

                Text("This app does not talk to the mouse itself. It runs "
                     + "egg-config and egg-flash and shows you what they say.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            .padding(24)
        }
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
