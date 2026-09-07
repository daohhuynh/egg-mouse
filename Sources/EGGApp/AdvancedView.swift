// AdvancedView.swift -- the read-only and offline half of the two CLIs.
//
// WHY THIS SCREEN EXISTS. An inventory on 2026-09-06 found that everything the
// tools can do WITHOUT touching the mouse was reachable only from a terminal:
// `diff`, `frames`, `encode`, the four-argument offline `dryrun`, the `devices`
// list, `read-firmware --check`, and `-v`. The app even printed "now run
// `egg-config diff ...`" in its own success text, sending people to Terminal at
// the moment it had just finished a firmware operation.
//
// Every verb on this screen either sends nothing at all or performs a read the
// tools already perform. Nothing here writes. That is the organising principle,
// and it is why they are together: a screen you cannot break is a screen worth
// exploring, and the alternative was thirteen more buttons on Settings.
//
// TWO THINGS ARE SHOWN AND DELIBERATELY NOT OFFERED, with the reason on screen:
// `--unknown-bytes` and `--vault`. See `policyNote`.
import SwiftUI

@MainActor
final class AdvancedModel: ObservableObject {
    @Published var output = ""
    @Published var busy = false
}

struct AdvancedView: View {
    @Binding var screen: Screen
    @EnvironmentObject var runner: ToolRunner
    @StateObject private var model = AdvancedModel()

    @State private var diffA = ""
    @State private var diffB = ""

    @State private var encRecord = ""
    @State private var encField = ""
    @State private var encValue = ""

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            ScreenHeader(title: "Advanced",
                         subtitle: "Everything the tools can do without "
                                 + "writing to the mouse.",
                         screen: $screen)

            HStack(alignment: .top, spacing: 16) {
                ScrollView {
                    VStack(alignment: .leading, spacing: 16) {
                        inspect
                        Divider()
                        compare
                        Divider()
                        offlinePreview
                        Divider()
                        policyNote
                    }
                    .padding(.trailing, 4)
                }
                .frame(width: 330)

                OutputPane(text: runner.liveOutput.isEmpty ? model.output
                                                           : runner.liveOutput)
            }
            .padding(.horizontal)
            .padding(.bottom)
            .disabled(model.busy)
        }
    }

    // ------------------------------------------------------------- sections

    @ViewBuilder private var inspect: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Inspect").font(.headline)

            Toggle("Hex-dump every frame (-v)", isOn: $runner.verbose)
                .help("A logging flag in both tools. It cannot change what is "
                    + "sent: the byte stream for a verb is identical with and "
                    + "without it, and Tests/test_config_set.sh checks that by "
                    + "diffing a dry run both ways.")

            Button("List every interface") {
                run("egg-config", Commands.listDevices())
            }
            .help("hid_enumerate and nothing else. Opens no handle, sends no "
                + "frame, and is safe on a mouse that is mid-recovery.")

            Button("Command frames (sends nothing)") {
                run("egg-config", Commands.frames())
            }
            .help("Every fixed command frame this build can put on the wire, "
                + "in hex. Entirely offline \u{2014} it is what the tool WOULD "
                + "send, not something it sends.")

            Button("Device info (A1 02)") {
                run("egg-config", Commands.deviceInfo())
            }
            .help("One small query. The vendor unpacks several dwords from the "
                + "reply; what they mean is not derived, so they are printed "
                + "and not named.")

            Button("Is the bootloader there?") {
                run("egg-flash", Commands.checkBootloader())
            }
            .help("`read-firmware --check`. Reports whether the mouse is in "
                + "the bootloader and stops \u{2014} it reads no blocks and "
                + "writes no file.")
        }
    }

    @ViewBuilder private var compare: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Compare two saved records").font(.headline)
            Text("Offline. This is the command the firmware screen tells you "
               + "to run after a flash, to check your settings survived.")
                .font(.caption).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)

            filePicker("Before", $diffA)
            filePicker("After", $diffB)

            HStack {
                Button("Known-good vs. after") {
                    diffA = Commands.knownGoodVaultPath()
                }
                .disabled(!FileManager.default
                              .fileExists(atPath: Commands.knownGoodVaultPath()))
                .help("Fills the first box with \(Commands.knownGoodVaultName), "
                    + "the copy egg-config saved by itself the first time it "
                    + "read this mouse.")
                Spacer()
                Button("Compare") {
                    run("egg-config", Commands.diffRecords(diffA, diffB))
                }
                .disabled(diffA.isEmpty || diffB.isEmpty)
            }
        }
    }

    @ViewBuilder private var offlinePreview: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Preview a change with the mouse unplugged").font(.headline)
            Text("The Settings screen previews a field change by opening the "
               + "device. This one works from a saved record and sends "
               + "nothing at all \u{2014} it prints the whole 1041-byte frame "
               + "the change would produce.")
                .font(.caption).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)

            filePicker("Record", $encRecord)
            TextField("Field (e.g. polling)", text: $encField)
            TextField("Value (e.g. 1000)", text: $encValue)

            HStack {
                Button("Show the frame") {
                    run("egg-config", Commands.dryRunField(record: encRecord,
                                                           field: encField,
                                                           value: encValue))
                }
                .disabled(encRecord.isEmpty || encField.isEmpty || encValue.isEmpty)
                Spacer()
                Button("Write a new record file\u{2026}") { saveEncoded() }
                    .disabled(encRecord.isEmpty || encField.isEmpty
                              || encValue.isEmpty)
                    .help("`encode` \u{2014} applies the field to the saved "
                        + "record and writes a NEW file. Touches no device "
                        + "and does not modify the input.")
            }

            Button("Decode a saved record") {
                let p = NSOpenPanel()
                p.canChooseFiles = true
                p.message = "Choose a settings record saved earlier"
                guard p.runModal() == .OK, let u = p.url else { return }
                run("egg-config", Commands.showSettings(from: u.path))
            }
            .help("`show --from` \u{2014} the same decoded table the Settings "
                + "screen shows, from a file.")
        }
    }

    @ViewBuilder private var policyNote: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("Shown, not offered").font(.headline)
            Text("`--unknown-bytes` chooses between two different byte "
               + "streams for record 0x01\u{2013}0x04. The tools default to "
               + "the vendor's, which is what all 73 captured host writes "
               + "carry; the other option sends a value no host has ever been "
               + "seen to send. That is a decision made once, in code, with "
               + "the evidence written beside it \u{2014} not a switch worth "
               + "flipping from a window.")
                .font(.caption).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
            Text("`--vault` moves where the automatic known-good copy lives. "
               + "It defaults to \(Commands.knownGoodVaultName) in your home "
               + "folder, and a second location is a second thing to lose.")
                .font(.caption).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    // -------------------------------------------------------------- helpers

    @ViewBuilder
    private func filePicker(_ label: String, _ path: Binding<String>) -> some View {
        HStack(spacing: 6) {
            Text(label).frame(width: 56, alignment: .leading)
            Text(path.wrappedValue.isEmpty
                 ? "\u{2014}"
                 : URL(fileURLWithPath: path.wrappedValue).lastPathComponent)
                .lineLimit(1).truncationMode(.middle)
                .foregroundStyle(path.wrappedValue.isEmpty ? .secondary : .primary)
            Spacer()
            Button("Choose\u{2026}") {
                let p = NSOpenPanel()
                p.canChooseFiles = true
                p.allowsMultipleSelection = false
                p.message = "Choose a settings record"
                if p.runModal() == .OK, let u = p.url { path.wrappedValue = u.path }
            }
        }
    }

    private func saveEncoded() {
        let p = NSSavePanel()
        p.message = "Where to write the modified record"
        p.nameFieldStringValue = "egg-mouse-edited.bin"
        guard p.runModal() == .OK, let u = p.url else { return }
        run("egg-config", Commands.encodeField(field: encField, value: encValue,
                                               from: encRecord, to: u.path))
    }

    private func run(_ tool: String, _ args: [String]) {
        Task {
            model.busy = true
            defer { model.busy = false }
            do {
                let r = try await runner.run(tool, args)
                model.output = "$ \(tool) " + args.joined(separator: " ")
                            + "\n\n" + r.text
            } catch {
                model.output = error.localizedDescription
            }
        }
    }
}
