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
    /// §7.25. Field name -> why THIS device will not accept it, from
    /// `egg-config read`. Empty until a read happens, and empty is the safe
    /// default: an older CLI prints no such section and the GUI must not
    /// respond to silence by disabling every field.
    @Published var gates: [String: String] = [:]
    @Published var buttons: [String] = []
    @Published var actions: [String] = []
    /// Action name -> the kind of argument it needs: "cpi", "key", or "none".
    /// Two of the nineteen actions take one, and until 2026-09-06 the app could
    /// not express either -- the picker offered them bare and every selection
    /// was a guaranteed refusal.
    @Published var actionArg: [String: String] = [:]
    /// The key names `key:` accepts, straight from the parser's own table.
    @Published var keyNames: [String] = []
    @Published var output = ""
    @Published var busy = false
    @Published var loadError: String?

    /// What the mouse is set to, decoded. Nil until someone asks -- reading it
    /// puts an A1 12 on the wire and this screen never does that unprompted.
    @Published var current: Commands.Shown?
    /// Where `current` came from, for the pane's own heading: the device, or a
    /// file. A decoded table with no provenance is the kind of thing someone
    /// acts on believing it came off the mouse.
    @Published var currentFrom = ""
    /// Set when a write succeeds after `current` was read. The values on
    /// screen are then older than the mouse, and saying so is free -- whereas
    /// re-reading automatically would send a frame nobody asked for (§4.2a).
    @Published var currentStale = false
    @Published var currentError: String?

    /// Load the decoded table. `from` nil means the device.
    func loadCurrent(_ runner: ToolRunner, from path: String? = nil) async {
        busy = true
        defer { busy = false }
        currentError = nil
        do {
            let r = try await runner.run(
                "egg-config", Commands.showSettingsMachine(from: path))
            guard r.ok else {
                currentError = r.text.isEmpty
                    ? "egg-config show exited non-zero." : r.text
                return
            }
            let parsed = Commands.parseShow(r.text)
            if parsed.isEmpty {
                currentError = "egg-config show returned nothing this app "
                             + "could parse. That is a build problem, not a "
                             + "device problem."
                return
            }
            current = parsed
            currentFrom = path.map { URL(fileURLWithPath: $0).lastPathComponent }
                       ?? "the mouse"
            currentStale = false

            // §7.25's gates, FROM THIS COMMAND TOO (added 2026-09-07).
            //
            // `model.gates` had exactly one producer: the `read` branch in
            // `run(_:ok:)`. "Show settings" is this screen's LEADING button and
            // it comes through here instead, so a user who pressed it saw "This
            // device will not accept a write here" in the right-hand pane while
            // Preview and Apply stayed enabled for that same field on the left,
            // and the CLI then refused the write. That contradicts this file's
            // own rule -- the GUI must not present a choice the tool will
            // reject -- and the data was already in hand: `show --machine`
            // carries the reason as the 6th column of every FIELD row and
            // `parseShow` has always kept it in `FieldValue.gate`.
            //
            // Only when the reading came off the DEVICE. A gate derived from a
            // saved file describes that file's record, and Preview and Apply
            // write to the mouse, so adopting it would gate the wrong thing.
            if path == nil {
                gates = Dictionary(
                    parsed.fields.filter { !$0.gate.isEmpty }
                                 .map { ($0.name, $0.gate) },
                    uniquingKeysWith: { first, _ in first })
            }
        } catch {
            currentError = error.localizedDescription
        }
    }

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
        // `map --machine`, NOT `map`. This used to scrape the human listing,
        // and an audit on 2026-09-06 found it offering `e.g.`, `for`, `minus`,
        // `grave` and `pagedown` -- continuation lines of two descriptions,
        // read as if they were actions. Five of twenty-four menu entries were
        // therefore guaranteed refusals, and the two actions that TAKE an
        // argument had nowhere to put one, so `key:` and `fixed-cpi:` bindings
        // were unreachable from the app entirely.
        //
        // The fix is to stop parsing prose. `--machine` emits the tables one
        // record per line with a leading keyword, and anything unrecognised is
        // ignored -- so a wording change in the human listing can no longer
        // reach this code.
        guard let r = try? await runner.run("egg-config", ["map", "--machine"])
        else { return }
        var b: [String] = [], a: [String] = [], k: [String] = []
        var arg: [String: String] = [:]
        for line in r.text.split(separator: "\n") {
            let f = line.split(separator: "\t", omittingEmptySubsequences: false)
                        .map(String.init)
            switch (f.first, f.count) {
            case ("BUTTON", 3):
                // f[2] == "1" means the vendor's own page offers a row for it.
                // `left` and `cpi-button` are 0 and the CLI refuses them, so
                // showing them would be presenting a choice that cannot work.
                if f[2] == "1" { b.append(f[1]) }
            case ("ACTION", 4):
                a.append(f[1]); arg[f[1]] = f[3]
            case ("KEY", 2):
                k.append(f[1])
            default:
                continue
            }
        }
        buttons = b
        actions = a
        actionArg = arg
        keyNames = k
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

    /// The right column shows one of two things. It is a switch and not a
    /// second window because they answer different questions -- "what is my
    /// mouse set to" and "what did the last command print" -- and a user who
    /// wants the first should not have to read the second to find it.
    enum RightPane: String, CaseIterable, Identifiable {
        case current = "Current settings", output = "Tool output"
        var id: String { rawValue }
    }
    @State private var rightPane: RightPane = .current

    @State private var selected: String = ""
    @State private var value: String = ""
    @State private var previewed = false

    @State private var button: String = ""
    @State private var action: String = ""
    /// The argument for the two actions that take one (fixed-cpi, key).
    @State private var argument: String = ""

    @State private var cpiStage: Int = 1
    @State private var cpiX: String = ""
    @State private var cpiY: String = ""
    @State private var cpiSplit = false

    @State private var mcButton = "left"
    @State private var mcMode = "off"
    @State private var mcValue = "8"

    @State private var side = "right"

    // The restore flow's two-step state. `restorePath` is set by the file
    // panel and survives the preview so the apply cannot target a different
    // file than the one that was shown.
    @State private var restorePath: String = ""
    @State private var restorePreviewed = false

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
                Button("Show settings") {
                    rightPane = .current
                    Task { await model.loadCurrent(runner) }
                }
                .help("One A1 12 read, decoded into words. The same exchange "
                    + "`Read from mouse` performs -- nothing extra goes out.")
                Button("Read from mouse") { run(Commands.readSettings()) }
                Button("Device info")     { run(Commands.deviceInfo()) }
                Button("Save a copy\u{2026}") { chooseSaveTarget() }
                    .help("Reads the record and writes it wherever you say. "
                        + "Until 2026-09-06 this always overwrote one fixed "
                        + "file in Documents, so two saved records could not "
                        + "coexist and the second silently replaced the first.")
                if FileManager.default.fileExists(
                        atPath: Commands.knownGoodVaultPath()) {
                    Button("Restore known-good") {
                        restorePath = Commands.knownGoodVaultPath()
                        restorePreviewed = false
                        run(Commands.previewRestore(record: restorePath)) {
                            restorePreviewed = true
                        }
                    }
                    .help("The copy egg-config saved by itself the first time "
                        + "it read this mouse (\(Commands.knownGoodVaultName)). "
                        + "It is never overwritten, so it is the oldest good "
                        + "state there is.")
                }
                Button("Restore a copy\u{2026}") { chooseRestore() }
                    .help("Pick a record saved earlier. It is PREVIEWED first, "
                        + "offline, with the mouse unplugged if you like -- "
                        + "the write needs a second click.")
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

            if !restorePath.isEmpty {
                HStack(spacing: 10) {
                    Text("Restore ")
                        + Text(URL(fileURLWithPath: restorePath).lastPathComponent)
                            .bold()
                    Spacer()
                    Button("Write it to the mouse") {
                        run(Commands.applyRestore(record: restorePath)) {
                            restorePath = ""; restorePreviewed = false
                        }
                    }
                    .disabled(!restorePreviewed)
                    .help(restorePreviewed
                          ? "Sends one A0 11, then reads the record back and "
                          + "verifies it byte for byte."
                          : "The preview did not succeed, so there is nothing "
                          + "to approve.")
                    Button("Cancel") { restorePath = ""; restorePreviewed = false }
                }
                .padding(.horizontal)
                .padding(.vertical, 6)
                .background(Color.secondary.opacity(0.12))
                .disabled(model.busy)
            }

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

                VStack(alignment: .leading, spacing: 8) {
                    Picker("", selection: $rightPane) {
                        ForEach(RightPane.allCases) { Text($0.rawValue).tag($0) }
                    }
                    .pickerStyle(.segmented)
                    .labelsHidden()
                    .frame(maxWidth: 320)

                    if rightPane == .current {
                        currentPane
                    } else {
                        OutputPane(text: runner.liveOutput.isEmpty
                                            ? model.output : runner.liveOutput)
                    }
                }
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

    // ------------------------------------------------- the decoded settings

    /// Everything the mouse is set to, in words.
    ///
    /// THE RULE THIS PANE FOLLOWS. It renders what `show --machine` said and
    /// nothing else. It does not compute a reading, does not fall back to a
    /// plausible default, and does not hide a row it cannot explain: a field
    /// whose byte is `undecodable` is shown in red with the CLI's own sentence,
    /// and a button entry that matched none of the 19 derived actions says
    /// UNKNOWN with its raw bytes beside it. §1.2a -- an absence shown as a
    /// blank is a claim.
    ///
    /// Split into one small view per section because SwiftUI's type checker
    /// gives up on the whole thing in one body. That is a real constraint, not
    /// a style choice, and it is worth a line here so nobody merges them back.
    @ViewBuilder private var currentPane: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 14) {
                if let e = model.currentError {
                    Text(e).foregroundStyle(.red)
                        .fixedSize(horizontal: false, vertical: true)
                }
                if let c = model.current {
                    shownHeader
                    shownFields(c.fields)
                    shownCpi(c.cpi)
                    shownButtons(c)
                    shownClicks(c.clicks)
                    shownRaw(c.raw)
                } else if model.currentError == nil {
                    shownEmpty
                }
            }
            .padding(12)
            .frame(maxWidth: .infinity, alignment: .leading)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .background(Color.secondary.opacity(0.06))
    }

    @ViewBuilder private var shownHeader: some View {
        HStack(spacing: 8) {
            Text("Read from \(model.currentFrom)")
                .font(.caption).foregroundStyle(.secondary)
            Spacer()
            Button("Refresh") { Task { await model.loadCurrent(runner) } }
            Button("From a file\u{2026}") { chooseShowFile() }
                .help("Decode a record saved earlier. Opens no device and "
                    + "sends nothing.")
        }
        if model.currentStale {
            Text("A write has succeeded since this was read, so these values "
               + "are older than the mouse. Refresh to send another read.")
                .font(.caption).bold()
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    @ViewBuilder private var shownEmpty: some View {
        Text("Nothing read yet.").foregroundStyle(.secondary)
        Text("`Show settings` sends one A1 12 read \u{2014} the same exchange "
           + "`Read from mouse` performs \u{2014} and decodes every byte it "
           + "understands. `From a file\u{2026}` does the same to a record "
           + "saved earlier, with nothing plugged in.")
            .font(.caption).foregroundStyle(.secondary)
            .fixedSize(horizontal: false, vertical: true)
        Button("From a file\u{2026}") { chooseShowFile() }
    }

    @ViewBuilder
    private func shownFields(_ fields: [Commands.Shown.FieldValue]) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            Text("Settings").font(.headline)
            ForEach(fields, id: \.name) { f in
                VStack(alignment: .leading, spacing: 1) {
                    HStack(alignment: .firstTextBaseline, spacing: 8) {
                        Text(f.name)
                            .font(.system(.body, design: .monospaced))
                            .frame(width: 170, alignment: .leading)
                        Text(f.text)
                            .foregroundStyle(f.ok ? Color.primary : Color.red)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                    Text(f.location)
                        .font(.caption2).foregroundStyle(.secondary)
                        .padding(.leading, 178)
                    if !f.gate.isEmpty {
                        Text("This device will not accept a write here: \(f.gate)")
                            .font(.caption).bold()
                            .padding(.leading, 178)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                }
            }
        }
    }

    @ViewBuilder
    private func shownCpi(_ stages: [Commands.Shown.Stage]) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            Text("CPI stages").font(.headline)
            ForEach(stages, id: \.index) { s in
                HStack(spacing: 8) {
                    Text("stage \(s.index)")
                        .font(.system(.body, design: .monospaced))
                        .frame(width: 90, alignment: .leading)
                    Text(s.x == s.y ? "\(s.x) CPI" : "\(s.x) x \(s.y) CPI")
                    if s.active { Text("active").font(.caption).bold() }
                    if !s.inUse {
                        Text("beyond CPI Levels; not selectable")
                            .font(.caption).foregroundStyle(.secondary)
                    }
                }
            }
        }
    }

    @ViewBuilder
    private func shownButtons(_ c: Commands.Shown) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            Text("Buttons").font(.headline)
            Text(c.handed == "unknown"
                 ? "Handedness: UNKNOWN \u{2014} neither entry holds the "
                 + "primary-click value, a state the vendor's own UI cannot "
                 + "produce."
                 : "Handedness: \(c.handed)-handed")
                .font(.caption).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
            ForEach(c.buttons, id: \.slot) { b in
                HStack(alignment: .firstTextBaseline, spacing: 8) {
                    Text(b.slot)
                        .font(.system(.body, design: .monospaced))
                        .frame(width: 90, alignment: .leading)
                    if b.unknown {
                        Text("UNKNOWN").bold().foregroundStyle(.red)
                        Text(b.detail).font(.caption).foregroundStyle(.secondary)
                            .fixedSize(horizontal: false, vertical: true)
                    } else {
                        Text(b.action)
                        if !b.detail.isEmpty {
                            Text(b.detail).foregroundStyle(.secondary)
                        }
                    }
                    if !b.offered {
                        Text("not editable here")
                            .font(.caption).foregroundStyle(.secondary)
                    }
                }
            }
        }
    }

    @ViewBuilder
    private func shownClicks(_ clicks: [Commands.Shown.Click]) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            Text("Click filter / SPDT").font(.headline)
            ForEach(clicks, id: \.button) { m in
                HStack(spacing: 8) {
                    Text(m.button)
                        .font(.system(.body, design: .monospaced))
                        .frame(width: 90, alignment: .leading)
                    if m.mode == "off" {
                        Text("filter \(m.value)")
                    } else if m.mode == "unknown" {
                        Text("raw 0x\(m.raw) \u{2014} neither a filter value "
                           + "nor a GX mode")
                            .foregroundStyle(.red)
                            .fixedSize(horizontal: false, vertical: true)
                    } else {
                        Text(m.mode)
                    }
                }
            }
        }
    }

    @ViewBuilder
    private func shownRaw(_ raw: [String: String]) -> some View {
        if !raw.isEmpty {
            VStack(alignment: .leading, spacing: 4) {
                Text("Derived, shown but not settable").font(.headline)
                ForEach(raw.keys.sorted(), id: \.self) { k in
                    HStack(spacing: 8) {
                        Text(k)
                            .font(.system(.body, design: .monospaced))
                            .frame(width: 170, alignment: .leading)
                        Text("0x\(raw[k] ?? "")").foregroundStyle(.secondary)
                    }
                }
                Text("`egg-config set` lists why each of these is withheld "
                   + "from writing. Reading one is free.")
                    .font(.caption).foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
    }

    private func chooseShowFile() {
        let p = NSOpenPanel()
        p.canChooseFiles = true
        p.allowsMultipleSelection = false
        p.message = "Choose a settings record saved earlier"
        guard p.runModal() == .OK, let u = p.url else { return }
        Task { await model.loadCurrent(runner, from: u.path) }
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
                        // §7.25. The tool would refuse this write; say so here
                        // rather than letting someone fill the form in first.
                        if let why = model.gates[f.name] {
                            Text("This device will not accept `\(f.name)`.")
                                .font(.caption).bold()
                            Text(why)
                                .font(.caption).foregroundStyle(.secondary)
                                .fixedSize(horizontal: false, vertical: true)
                        }
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
                            let gated = model.gates[f.name] != nil
                            Button("Preview") {
                                previewed = true
                                run(Commands.previewSet(field: f.name, value: value))
                            }
                            .disabled(value.isEmpty || gated)
                            Button("Apply") { run(Commands.applySet(field: f.name, value: value)) }
                                .disabled(!previewed || value.isEmpty || gated)
                                .help(gated ? "This device will not accept this field."
                                            : previewed ? "Write it, read it back, verify."
                                                        : "Preview it first.")
                        }
                    }

                    withheldList
        }
    }

    /// "Why is glass-mode not in the list?"
    ///
    /// The CLI prints a paragraph for each field it derived and deliberately
    /// does NOT offer, and the app has parsed those into `model.withheld` since
    /// the day it shipped -- and never rendered them. So a user looking for
    /// `glass-mode`, `multiclick-filter` or the vendor's inverted "Disable LED
    /// on Lift-Off" saw no trace of it and no explanation. CLAUDE.md §1.2a: an
    /// absence is a claim, and that rule applies to our own UI.
    @ViewBuilder private var withheldList: some View {
        if !model.withheld.isEmpty {
            DisclosureGroup("Derived, deliberately not settable "
                          + "(\(model.withheld.count))") {
                VStack(alignment: .leading, spacing: 8) {
                    ForEach(model.withheld, id: \.0) { name, why in
                        VStack(alignment: .leading, spacing: 2) {
                            Text(name)
                                .font(.system(.caption, design: .monospaced))
                                .bold()
                            Text(why)
                                .font(.caption).foregroundStyle(.secondary)
                                .fixedSize(horizontal: false, vertical: true)
                        }
                    }
                }
                .padding(.top, 4)
            }
            .font(.caption)
            .padding(.top, 6)
        }
    }

    /// `fixed-cpi` plus "1600" is the single token `fixed-cpi:1600` on the
    /// command line. Built in ONE place so preview and apply cannot disagree
    /// about what is being asked for -- the whole point of a preview is that it
    /// showed you the thing that then happens.
    private var composedAction: String {
        let kind = model.actionArg[action] ?? "none"
        if kind == "none" { return action }
        let a = argument.trimmingCharacters(in: .whitespaces)
        return a.isEmpty ? action : action + ":" + a
    }

    /// Why the mapping buttons are disabled, as a Bool. An action that needs an
    /// argument and has none would produce `egg-config map right fixed-cpi`,
    /// which the CLI refuses -- so the app must not offer to send it.
    private var mapBlocked: Bool {
        if button.isEmpty || action.isEmpty { return true }
        let kind = model.actionArg[action] ?? "none"
        if kind != "none" && argument.trimmingCharacters(in: .whitespaces).isEmpty {
            return true
        }
        return false
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
            .onChange(of: action) { _, _ in previewed = false; argument = "" }

            // The two actions that take an argument. Shown only for those two,
            // because an always-visible field would imply the other seventeen
            // accept one.
            if model.actionArg[action] == "cpi" {
                TextField("CPI, e.g. 1600 or 1600x800", text: $argument)
                    .onChange(of: argument) { _, _ in previewed = false }
                Text("10 to 30000 in steps of 10. Give one number for both axes, "
                   + "or XxY for independent ones.")
                    .font(.caption).foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            } else if model.actionArg[action] == "key" {
                TextField("Key, e.g. a  ctrl+shift+a  f5", text: $argument)
                    .onChange(of: argument) { _, _ in previewed = false }
                Text("Modifiers ctrl, shift, alt and gui may be combined with +. "
                   + "Names: " + model.keyNames.prefix(14).joined(separator: " ")
                   + (model.keyNames.count > 14 ? " …" : ""))
                    .font(.caption).foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }

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
                    run(Commands.previewMap(button: button, action: composedAction))
                }
                .disabled(mapBlocked)
                Button("Apply") { run(Commands.applyMap(button: button, action: composedAction)) }
                    .disabled(!previewed || mapBlocked)
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

    /// Two clicks, never one. The first is offline (`dryrun`) and shows the
    /// whole frame plus the diff; the second is the only one that opens the
    /// device. Deliberately NOT modelled as a confirmation alert: an alert
    /// asks "are you sure" about a thing the user has not been shown, which is
    /// the reflex CLAUDE.md 4.2c exists to refuse. Here the second click comes
    /// after the bytes are on screen.
    private func chooseRestore() {
        let p = NSOpenPanel()
        p.canChooseFiles = true
        p.allowsMultipleSelection = false
        p.message = "Choose a settings record saved earlier"
        guard p.runModal() == .OK, let u = p.url else { return }
        restorePath = u.path
        restorePreviewed = false
        run(Commands.previewRestore(record: u.path)) { restorePreviewed = true }
    }

    /// A save panel, not a fixed path. The old fixed path meant every click of
    /// "Save a copy" overwrote the previous one -- so a user who saved before a
    /// change and again after it had only the second, and the whole point of
    /// saving was to be able to compare them (`Advanced` -> Compare).
    private func chooseSaveTarget() {
        let p = NSSavePanel()
        p.message = "Where to save this settings record"
        p.nameFieldStringValue = defaultSaveName()
        guard p.runModal() == .OK, let u = p.url else { return }
        run(Commands.readSettings(savingTo: u.path))
    }

    /// Dated, so the panel's own default no longer collides with itself.
    private func defaultSaveName() -> String {
        let f = DateFormatter()
        f.dateFormat = "yyyy-MM-dd-HHmm"
        return "egg-mouse-settings-\(f.string(from: Date())).bin"
    }

    /// `ok` is called only when egg-config both ran AND exited 0. The
    /// distinction matters for restore: a preview that refused (a short file,
    /// an implausible record) must NOT arm the write button, and `try await`
    /// alone would not tell them apart -- ToolRunner throws when the tool
    /// cannot be launched, not when the tool says no.
    private func run(_ args: [String], ok: (() -> Void)? = nil) {
        Task {
            model.busy = true
            defer { model.busy = false }
            do {
                let r = try await runner.run("egg-config", args)
                if r.ok { ok?() }
                model.output = "$ egg-config " + args.joined(separator: " ")
                            + "\n\n" + r.text
                // §7.25. A read is the only command that reports which fields
                // THIS device will not accept, so it is the only one that may
                // update the gates. Deliberately not cleared by other commands:
                // a stale "lod is gated" is a refusal the CLI would repeat
                // anyway, while a cleared one would re-offer a field that is
                // still unsafe.
                if args.first == "read" { model.gates = Commands.parseGates(r.text) }
                // A write that succeeded makes the decoded table older than
                // the mouse. Said, not silently corrected: correcting it means
                // another A1 12, and this screen does not send frames the user
                // did not ask for.
                if r.ok && args.contains("--yes") && model.current != nil {
                    model.currentStale = true
                }
            } catch {
                model.output = error.localizedDescription
            }
        }
    }
}
