// ConfigView.swift -- settings.
//
// The screen parses `egg-config set` (the field list) and `egg-config map`
// (the button menu) rather than carrying its own copies. That is deliberate:
// the CLI's tables are the ones cited to cfg107 addresses and checked by
// Tests/test_citations.py against raw bytes. A second list here would be a
// second thing to keep true, and the first time it drifted it would offer a
// field the tool refuses -- or worse, name a field after the wrong record.
//
// Nothing here writes without a dry run first. `egg-config set` prints the
// exact byte that would change and requires --yes; this screen shows that
// output and only then offers Apply.
import SwiftUI

/// The view's row type: Commands.Field plus Identifiable.
struct Field: Identifiable {
    let f: Commands.Field
    var id: String { f.name }
    var name: String { f.name }
    var record: String { f.record }
    var accepts: String { f.accepts }
    var cite: String { f.cite }
    var choices: [String]? { Commands.choices(for: f.accepts) }
}

@MainActor
final class ConfigModel: ObservableObject {
    @Published var fields: [Field] = []
    @Published var withheld: [(String, String)] = []
    @Published var buttons: [String] = []
    @Published var actions: [String] = []
    @Published var output = ""
    @Published var busy = false
    @Published var loadError: String?

    /// Loads the field table by asking the CLI and parsing its answer with
    /// Commands.parseFields, which Tests/test_app_commands.swift drives against
    /// the real output.
    func loadFields(_ runner: ToolRunner) async {
        busy = true
        defer { busy = false }
        do {
            let r = try await runner.run("egg-config", ["set"])
            let parsed = Commands.parseFields(r.text)
            fields = parsed.fields.map { Field(f: $0) }
            withheld = parsed.withheld
            if fields.isEmpty {
                loadError = "egg-config listed no settable fields. That is a "
                          + "build problem, not a device problem."
            }
        } catch {
            loadError = error.localizedDescription
        }
    }

    /// Parse `egg-config map` for the button and action names.
    ///
    /// TWO THINGS THIS HAS TO GET RIGHT, both found by reading the CLI's actual
    /// output rather than by assuming its shape (2026-09-06):
    ///
    ///  1. The action list is NESTED. `  mouse:` and `  cpi:` are group headers
    ///     at the same two-space indent as everything else, so the first
    ///     version offered `mouse:` as a bindable action. `egg-config map right
    ///     mouse:` is not a command; the picker was offering the user an error.
    ///     Any line ending in `:` is a heading, never a name.
    ///  2. Some buttons are marked `[not offered]` -- `left` and `cpi-button`,
    ///     which the CLI refuses on purpose (a mouse with no left click cannot
    ///     be un-configured, and the CPI button is how you get back). Offering
    ///     them would produce a refusal the user cannot act on. The GUI must
    ///     not present a choice the tool will reject.
    func loadButtons(_ runner: ToolRunner) async {
        guard let r = try? await runner.run("egg-config", ["map"]) else { return }
        var b: [String] = [], a: [String] = []
        var mode = 0
        for line in r.text.split(separator: "\n") {
            let s = String(line)
            if s.lowercased().contains("button") && s.hasSuffix(":") { mode = 1; continue }
            if s.lowercased().contains("action") && s.hasSuffix(":") { mode = 2; continue }
            let t = s.trimmingCharacters(in: .whitespaces)
            guard s.hasPrefix("  "), !t.isEmpty, !t.hasSuffix(":") else { continue }
            let word = String(t.split(separator: " ").first ?? "")
            guard !word.isEmpty else { continue }
            if mode == 1 {
                if !t.contains("not offered") { b.append(word) }
            } else if mode == 2 {
                a.append(word)
            }
        }
        buttons = b
        actions = a
    }
}

struct ConfigView: View {
    @Binding var screen: Screen
    @EnvironmentObject var runner: ToolRunner
    @StateObject private var model = ConfigModel()

    /// Which of the three editors the left column is showing. They write three
    /// different record regions through three different CLI verbs, so they are
    /// separate panels rather than one clever form -- and switching clears the
    /// preview, because a preview belongs to the exact command that produced it
    /// (CLAUDE.md §4.2c: an approval is bound to the bytes it approves).
    enum Editor: String, CaseIterable, Identifiable {
        case field = "One field", button = "Buttons", cpi = "CPI"
        case click = "Click filter", hand = "Handedness"
        var id: String { rawValue }
    }
    @State private var editor: Editor = .field

    @State private var selected: String = ""
    @State private var value: String = ""
    @State private var previewed = false

    @State private var button: String = ""
    @State private var action: String = ""

    @State private var cpiStage: Int = 1
    @State private var cpiX: String = ""
    @State private var cpiY: String = ""
    @State private var cpiSplit = false

    @State private var mcButton = "left"
    @State private var mcMode = "off"
    @State private var mcValue = "8"

    @State private var side = "right"

    var field: Field? { model.fields.first { $0.name == selected } }

    /// The X/Y the CPI panel would send, or nil if either is not on the grid.
    /// Commands.cpiIsLegal is scored against the CLI by
    /// Tests/test_app_commands.swift, so this cannot quietly drift from what
    /// egg-config will accept.
    var cpiValues: (x: Int, y: Int?)? {
        guard let x = Int(cpiX.trimmingCharacters(in: .whitespaces)),
              Commands.cpiIsLegal(x) else { return nil }
        if !cpiSplit { return (x, nil) }
        guard let y = Int(cpiY.trimmingCharacters(in: .whitespaces)),
              Commands.cpiIsLegal(y) else { return nil }
        return (x, y)
    }

    /// What the vendor's own tool would have stored instead. Shown rather than
    /// silently corrected: the user typed a number and is entitled to know
    /// where it lands, and §1.3's spirit is that nothing is changed behind the
    /// user's back -- not even a number in a box.
    func cpiHint(_ s: String) -> String? {
        let t = s.trimmingCharacters(in: .whitespaces)
        guard !t.isEmpty, let v = Int(t) else { return t.isEmpty ? nil : "not a number" }
        if Commands.cpiIsLegal(v) { return nil }
        return "off the grid -- the vendor's tool would store \(Commands.normaliseCpi(v))"
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            ScreenHeader(title: "Settings",
                         subtitle: "Read, change one field at a time, verified "
                                 + "on the device after every write.",
                         screen: $screen)

            if let e = model.loadError {
                Text(e).foregroundStyle(.red).padding(.horizontal)
            }

            HStack(spacing: 10) {
                Button("Read from mouse") { run(Commands.readSettings()) }
                Button("Device info")     { run(Commands.deviceInfo()) }
                Button("Save a copy")     { run(Commands.readSettings(savingTo: defaultSavePath())) }
                Spacer()
                Button(role: .destructive) {
                    run(Commands.factoryReset())
                } label: { Text("Factory reset") }
                    .help("A1 13. The device composes its own defaults; this "
                        + "tool sends no settings bytes. Your saved copy is "
                        + "the way back.")
            }
            .padding(.horizontal)
            .disabled(model.busy)

            Divider()

            HStack(alignment: .top, spacing: 16) {
                VStack(alignment: .leading, spacing: 8) {
                    Picker("", selection: $editor) {
                        ForEach(Editor.allCases) { Text($0.rawValue).tag($0) }
                    }
                    .pickerStyle(.segmented)
                    .labelsHidden()
                    .onChange(of: editor) { _, _ in previewed = false }

                    switch editor {
                    case .field:  fieldEditor
                    case .button: buttonEditor
                    case .cpi:    cpiEditor
                    case .click:  multiclickEditor
                    case .hand:   handednessEditor
                    }
                    Spacer()
                }
                .frame(width: 300)

                OutputPane(text: runner.liveOutput.isEmpty ? model.output
                                                           : runner.liveOutput)
            }
            .padding(.horizontal)
            .padding(.bottom)
            .disabled(model.busy)
        }
        .task {
            await model.loadFields(runner)
            await model.loadButtons(runner)
        }
    }

    // ---------------------------------------------------------------- panels

    @ViewBuilder private var fieldEditor: some View {
        VStack(alignment: .leading, spacing: 8) {
                    Text("Change one field").font(.headline)
                    Picker("Field", selection: $selected) {
                        Text("—").tag("")
                        ForEach(model.fields) { f in Text(f.name).tag(f.name) }
                    }
                    .onChange(of: selected) { _, _ in previewed = false; value = "" }

                    if let f = field {
                        Text(f.record).font(.caption).foregroundStyle(.secondary)
                        if let choices = f.choices {
                            Picker("Value", selection: $value) {
                                Text("—").tag("")
                                ForEach(choices, id: \.self) { Text($0).tag($0) }
                            }
                            .onChange(of: value) { _, _ in previewed = false }
                        } else {
                            TextField("Value", text: $value)
                                .onChange(of: value) { _, _ in previewed = false }
                        }
                        Text("Accepts: \(f.accepts)")
                            .font(.caption).foregroundStyle(.secondary)
                            .fixedSize(horizontal: false, vertical: true)
                        if !f.cite.isEmpty {
                            Text(f.cite)
                                .font(.system(size: 10, design: .monospaced))
                                .foregroundStyle(.tertiary)
                                .fixedSize(horizontal: false, vertical: true)
                        }

                        HStack {
                            // Dry run first, always. §4.3 wants a dry-run seam
                            // and this is where a person uses it.
                            Button("Preview") {
                                previewed = true
                                run(Commands.previewSet(field: f.name, value: value))
                            }
                            .disabled(value.isEmpty)
                            Button("Apply") { run(Commands.applySet(field: f.name, value: value)) }
                                .disabled(!previewed || value.isEmpty)
                                .help(previewed ? "Write it, read it back, verify."
                                                : "Preview it first.")
                        }
                    }
        }
    }

    @ViewBuilder private var buttonEditor: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Rebind a button").font(.headline)
            if model.buttons.isEmpty {
                Text("egg-config listed no rebindable buttons.")
                    .font(.caption).foregroundStyle(.secondary)
            }
            Picker("Button", selection: $button) {
                Text("—").tag("")
                ForEach(model.buttons, id: \.self) { Text($0).tag($0) }
            }
            .onChange(of: button) { _, _ in previewed = false }
            Picker("Action", selection: $action) {
                Text("—").tag("")
                ForEach(model.actions, id: \.self) { Text($0).tag($0) }
            }
            .onChange(of: action) { _, _ in previewed = false }

            // Left click and the CPI button are absent from the list on
            // purpose; say why rather than leaving a person hunting for them.
            Text("`left` and `cpi-button` are not offered: a mouse whose left "
               + "click has been rebound cannot easily be put back, and the "
               + "CPI button is the way out of a bad CPI.")
                .font(.caption).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)

            HStack {
                Button("Preview") {
                    previewed = true
                    run(Commands.previewMap(button: button, action: action))
                }
                .disabled(button.isEmpty || action.isEmpty)
                Button("Apply") { run(Commands.applyMap(button: button, action: action)) }
                    .disabled(!previewed || button.isEmpty || action.isEmpty)
                    .help(previewed ? "Write it, read it back, verify."
                                    : "Preview it first.")
            }
        }
    }

    @ViewBuilder private var multiclickEditor: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Click filter / SPDT").font(.headline)
            Picker("Button", selection: $mcButton) {
                ForEach(Commands.multiclickButtons, id: \.self) { Text($0).tag($0) }
            }
            .onChange(of: mcButton) { _, b in
                previewed = false
                // Switching to a button with no SPDT combo must not leave a GX
                // mode selected -- the CLI would refuse it and the user would
                // see a rejection they did not cause.
                if !Commands.multiclickModes(for: b).contains(mcMode) { mcMode = "off" }
            }
            Picker("Mode", selection: $mcMode) {
                ForEach(Commands.multiclickModes(for: mcButton), id: \.self) {
                    Text($0).tag($0)
                }
            }
            .onChange(of: mcMode) { _, _ in previewed = false }
            if mcMode == "off" {
                TextField("Filter (0-25)", text: $mcValue)
                    .onChange(of: mcValue) { _, _ in previewed = false }
            }
            Text(Commands.multiclickModes(for: mcButton).count > 1
                 ? "This button has an SPDT combo, so it can also be set to a "
                 + "GX mode. The byte is SHARED \u{2014} setting one erases the other."
                 : "Only the left and right buttons have an SPDT combo on the "
                 + "vendor's page, so this one takes a number and nothing else.")
                .font(.caption).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
            Text("derived: config-protocol.md §7.22, cfg107 0x405f84 / 0x406645")
                .font(.system(size: 10, design: .monospaced))
                .foregroundStyle(.tertiary)
                .fixedSize(horizontal: false, vertical: true)
            HStack {
                Button("Preview") {
                    previewed = true
                    run(Commands.previewMulticlick(button: mcButton, mode: mcMode,
                                                   value: Int(mcValue)))
                }
                .disabled(!mcLegal)
                Button("Apply") {
                    run(Commands.applyMulticlick(button: mcButton, mode: mcMode,
                                                 value: Int(mcValue)))
                }
                .disabled(!previewed || !mcLegal)
            }
        }
    }

    private var mcLegal: Bool {
        Commands.multiclickIsLegal(mode: mcMode, value: Int(mcValue),
                                   button: mcButton)
    }

    @ViewBuilder private var handednessEditor: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Handedness").font(.headline)
            Picker("Primary click", selection: $side) {
                Text("right-handed").tag("right")
                Text("left-handed").tag("left")
            }
            .pickerStyle(.radioGroup)
            .onChange(of: side) { _, _ in previewed = false }

            // Deliberately NOT shown as a toggle reflecting a known state. This
            // is not a flag byte: the CLI reads the record, works out which
            // entry holds the mapping, and refuses if it is in neither state.
            // A checkbox would imply the app knows the answer before asking.
            Text("Not a switch. The vendor implements this by MOVING your "
               + "button assignment between the left and right entries, so "
               + "Preview reads the mouse first and shows exactly which bytes "
               + "would change. If the record is in neither state it refuses "
               + "rather than guess which entry holds your mapping.")
                .font(.caption).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
            Text("derived: config-protocol.md §7.20, cfg107 0x408b00")
                .font(.system(size: 10, design: .monospaced))
                .foregroundStyle(.tertiary)
                .fixedSize(horizontal: false, vertical: true)

            HStack {
                Button("Preview") {
                    previewed = true
                    run(Commands.previewHandedness(side))
                }
                Button("Apply") { run(Commands.applyHandedness(side)) }
                    .disabled(!previewed)
                    .help(previewed ? "Write it, read it back, verify."
                                    : "Preview it first.")
            }
        }
    }

    @ViewBuilder private var cpiEditor: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("CPI stages").font(.headline)
            Picker("Stage", selection: $cpiStage) {
                ForEach(1...4, id: \.self) { Text("CPI \($0)").tag($0) }
            }
            .onChange(of: cpiStage) { _, _ in previewed = false }

            TextField(cpiSplit ? "X" : "CPI", text: $cpiX)
                .onChange(of: cpiX) { _, _ in previewed = false }
            if let h = cpiHint(cpiX) {
                Text(h).font(.caption).foregroundStyle(.orange)
                    .fixedSize(horizontal: false, vertical: true)
            }

            Toggle("Separate X and Y", isOn: $cpiSplit)
                .onChange(of: cpiSplit) { _, _ in previewed = false }
            if cpiSplit {
                TextField("Y", text: $cpiY)
                    .onChange(of: cpiY) { _, _ in previewed = false }
                if let h = cpiHint(cpiY) {
                    Text(h).font(.caption).foregroundStyle(.orange)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }

            Text("10 to 10000 in steps of 10, then 10050 to 30000 in steps of "
               + "50. The X≠Y flag byte is computed, never typed.")
                .font(.caption).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
            Text("derived: config-protocol.md §7.19, cfg107 0x40d880-0x40d93a")
                .font(.system(size: 10, design: .monospaced))
                .foregroundStyle(.tertiary)
                .fixedSize(horizontal: false, vertical: true)

            if cpiSplit && cpiStage == 4 {
                Text("Note: for stage 4 only, the vendor's own tool reads the "
                   + "X≠Y flag off stage 3's boxes (a bug at 0x40edde). This "
                   + "tool computes it from stage 4, so that one byte can "
                   + "differ from what Windows would send. No observation of "
                   + "stage 4 with X≠Y exists either way.")
                    .font(.caption).foregroundStyle(.orange)
                    .fixedSize(horizontal: false, vertical: true)
            }

            HStack {
                Button("Preview") {
                    previewed = true
                    if let v = cpiValues {
                        run(Commands.previewCpi(stage: cpiStage, x: v.x, y: v.y))
                    }
                }
                .disabled(cpiValues == nil)
                Button("Apply") {
                    if let v = cpiValues {
                        run(Commands.applyCpi(stage: cpiStage, x: v.x, y: v.y))
                    }
                }
                .disabled(!previewed || cpiValues == nil)
                .help(previewed ? "Write it, read it back, verify."
                                : "Preview it first.")
            }
        }
    }

    private func defaultSavePath() -> String {
        let d = FileManager.default.urls(for: .documentDirectory,
                                         in: .userDomainMask).first!
        return d.appendingPathComponent("egg-mouse-settings.bin").path
    }

    private func run(_ args: [String]) {
        Task {
            model.busy = true
            defer { model.busy = false }
            do {
                let r = try await runner.run("egg-config", args)
                model.output = "$ egg-config " + args.joined(separator: " ")
                            + "\n\n" + r.text
            } catch {
                model.output = error.localizedDescription
            }
        }
    }
}
