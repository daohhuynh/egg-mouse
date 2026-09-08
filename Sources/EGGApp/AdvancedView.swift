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

    /// The button and action names the CLI will actually accept, from
    /// `map --machine`. Loaded once when the screen appears; empty until then,
    /// and empty is handled on screen rather than by guessing a list -- a
    /// hardcoded copy here would be a second answer to "what can be bound",
    /// and the first one is the tool's.
    @Published var tables = Commands.ButtonTables()

    /// `map --machine` opens no device -- it prints compiled-in tables. Safe
    /// on the Advanced screen, whose whole claim is that nothing here writes.
    func loadTables(_ runner: ToolRunner) async {
        guard tables.buttons.isEmpty else { return }
        guard let r = try? await runner.run("egg-config", Commands.mapMachine())
        else { return }
        tables = Commands.parseButtonTables(r.text)
    }
}

/// Which run verb the offline preview is building. The four are separate
/// because they take different arguments and write different record regions,
/// not because they are different in kind -- all four are `<verb> ... --from`.
enum PreviewVerb: String, CaseIterable, Identifiable {
    case map = "map", cpi = "cpi", multiclick = "multiclick"
    case handedness = "handedness"
    var id: String { rawValue }
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
    /// Which side the offline handedness preview asks for. `handedness` is not
    /// one of `set`'s fields, so the three boxes above cannot reach it.
    @State private var handSide = "left"

    // The offline run-verb preview (2026-09-07). `--from` belongs in the app as
    // well as the CLI: it is something more advanced users will want to reach.
    @State private var pvVerb: PreviewVerb = .map
    @State private var pvButton = ""
    @State private var pvAction = ""
    @State private var pvActionArg = ""
    @State private var pvStage = 1
    @State private var pvX = ""
    @State private var pvY = ""
    @State private var pvMcButton = "left"
    @State private var pvMcMode = "off"
    @State private var pvMcValue = "8"

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

            Divider().padding(.vertical, 2)
            runVerbPreview
        }
    }

    // THE OFFLINE RUN-VERB PREVIEW.
    //
    // `map`, `cpi`, `multiclick` and `handedness` write a contiguous RUN of
    // record bytes rather than a single field, so `dryrun` -- which takes a
    // FIELD and a value -- cannot express any of them. Until 2026-09-07 the
    // CLI could not either: the four had no `--from`, and their `--yes`-less
    // previews printed the few bytes of the run without ever showing a frame.
    // `map`'s could not even print byte +6 honestly, because with no record in
    // hand there was nothing to copy through, so it printed `??`.
    //
    // Handedness was here alone before that, and only because it was the one
    // verb that already had `--from`. Now all four do, and they are one
    // section: the difference between them is which arguments they take, not
    // what the button does.
    //
    // WHY THE PICKERS COME FROM THE TOOL. `map --machine` is the source of the
    // button and action names, through the same parser ConfigView uses. A
    // hardcoded list here would be a second answer to "what can be bound", and
    // the audit of 2026-09-06 found exactly that going wrong on the Settings
    // screen -- five of twenty-four menu entries were guaranteed refusals.
    @ViewBuilder private var runVerbPreview: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Preview a button, CPI or click-filter change")
                .font(.subheadline).bold()
            Text("The four verbs that rewrite a RUN of record bytes. Each one "
               + "prints the whole frame it would send and the diff against "
               + "your file. `--from` and `--yes` together are refused by the "
               + "CLI, so nothing on this screen can become a write.")
                .font(.caption).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)

            Picker("", selection: $pvVerb) {
                ForEach(PreviewVerb.allCases) { Text($0.rawValue).tag($0) }
            }
            .pickerStyle(.segmented).labelsHidden()

            switch pvVerb {
            case .map:        mapArgs
            case .cpi:        cpiArgs
            case .multiclick: multiclickArgs
            case .handedness: handednessArgs
            }

            HStack {
                Spacer()
                Button("Show what it would write\u{2026}") { runPreview() }
                    .disabled(!previewIsReady)
                    .help(previewHelp)
            }
        }
        .task { await model.loadTables(runner) }
    }

    @ViewBuilder private var mapArgs: some View {
        if model.tables.buttons.isEmpty {
            Text("egg-config listed no rebindable buttons.")
                .font(.caption).foregroundStyle(.secondary)
        } else {
            Picker("Button", selection: $pvButton) {
                ForEach(model.tables.buttons, id: \.self) { Text($0).tag($0) }
            }
            Picker("Action", selection: $pvAction) {
                ForEach(model.tables.actions, id: \.self) { Text($0).tag($0) }
            }
            // Two of the nineteen actions take an argument. The tool says
            // WHICH two (`ACTION\tname\tgroup\targ`), so the box appears
            // because the CLI said so rather than because the app remembers.
            if let kind = model.tables.actionArg[pvAction], kind != "none" {
                TextField(kind == "cpi" ? "CPI, e.g. 1600 or 1600x800"
                                        : "key, e.g. a  or  ctrl+shift+a",
                          text: $pvActionArg)
            }
        }
    }

    @ViewBuilder private var cpiArgs: some View {
        HStack {
            Picker("Stage", selection: $pvStage) {
                ForEach(1...4, id: \.self) { Text("CPI \($0)").tag($0) }
            }
            .frame(width: 150)
            Spacer()
        }
        TextField("X, e.g. 1600", text: $pvX)
        TextField("Y \u{2014} leave empty to match X", text: $pvY)
        Text("The X\u{2260}Y flag byte is computed by the tool, never typed. "
           + "For stage 4 the vendor's own tool computes it from stage 3's "
           + "boxes; we do not, and the preview says so when it matters.")
            .font(.caption).foregroundStyle(.secondary)
            .fixedSize(horizontal: false, vertical: true)
    }

    @ViewBuilder private var multiclickArgs: some View {
        Picker("Button", selection: $pvMcButton) {
            ForEach(Commands.multiclickButtons, id: \.self) { Text($0).tag($0) }
        }
        Picker("Mode", selection: $pvMcMode) {
            ForEach(Commands.multiclickModes(for: pvMcButton), id: \.self) {
                Text($0).tag($0)
            }
        }
        if pvMcMode == "off" {
            TextField("Filter, 0\u{2013}25", text: $pvMcValue)
        }
    }

    @ViewBuilder private var handednessArgs: some View {
        Picker("Side", selection: $handSide) {
            Text("left").tag("left")
            Text("right").tag("right")
        }
        .pickerStyle(.segmented).frame(width: 180)
        Text("§7.20: not a flag. It MOVES your mapping between entries 0 and "
           + "1, so the whole button block changes \u{2014} which is why "
           + "seeing the plan first is worth more here than anywhere else.")
            .font(.caption).foregroundStyle(.secondary)
            .fixedSize(horizontal: false, vertical: true)
    }

    // -------------------------------------------------- the preview's argv

    /// Is there a complete command to build? The GUI refuses to ASK rather
    /// than sending something the CLI will reject -- the same principle as
    /// leaving `left` and `cpi-button` out of the picker.
    private var previewIsReady: Bool {
        switch pvVerb {
        case .map:
            guard !pvButton.isEmpty, !pvAction.isEmpty else { return false }
            let kind = model.tables.actionArg[pvAction] ?? "none"
            return kind == "none" || !pvActionArg.isEmpty
        case .cpi:
            return Int(pvX.trimmingCharacters(in: .whitespaces)) != nil
        case .multiclick:
            return Commands.multiclickIsLegal(
                mode: pvMcMode,
                value: Int(pvMcValue.trimmingCharacters(in: .whitespaces)),
                button: pvMcButton)
        case .handedness:
            return true
        }
    }

    private var previewHelp: String {
        "`\(pvVerb.rawValue) \u{2026} --from FILE` \u{2014} prints the whole "
        + "1041-byte frame it would send and the diff against your record. "
        + "Opens no device and sends nothing."
    }

    /// The argv, or nil if the boxes do not make one. Built from the SAME
    /// `Commands` functions the writing path uses, plus `--from` -- so what
    /// this shows you is the command that would run, not a description of it.
    private func previewArgs(record: String) -> [String]? {
        switch pvVerb {
        case .map:
            let kind = model.tables.actionArg[pvAction] ?? "none"
            let spec = kind == "none" ? pvAction : "\(pvAction):\(pvActionArg)"
            return Commands.previewMap(button: pvButton, action: spec,
                                       from: record)
        case .cpi:
            guard let x = Int(pvX.trimmingCharacters(in: .whitespaces))
            else { return nil }
            let y = Int(pvY.trimmingCharacters(in: .whitespaces))
            return Commands.previewCpi(stage: pvStage, x: x, y: y, from: record)
        case .multiclick:
            let v = Int(pvMcValue.trimmingCharacters(in: .whitespaces))
            return Commands.previewMulticlick(button: pvMcButton,
                                              mode: pvMcMode, value: v,
                                              from: record)
        case .handedness:
            return Commands.previewHandedness(handSide, from: record)
        }
    }

    private func runPreview() {
        let p = NSOpenPanel()
        p.canChooseFiles = true
        p.allowsMultipleSelection = false
        p.message = "Choose a settings record saved earlier"
        guard p.runModal() == .OK, let u = p.url else { return }
        guard let args = previewArgs(record: u.path) else { return }
        run("egg-config", args)
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
