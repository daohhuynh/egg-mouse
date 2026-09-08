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
    /// Action name -> the group the CLI files it under. The Button Mapping
    /// menu is two levels deep because the vendor's is, and this is where the
    /// second level comes from -- read from `map --machine`, never grouped by
    /// a table of our own.
    @Published var actionCategory: [String: String] = [:]
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
        // The PARSE lives in Commands.parseButtonTables. It was inline here
        // until 2026-09-07, when the Advanced screen needed the same tables
        // for its offline previews -- and a second copy of this loop would
        // have been free to disagree with this one about which buttons the
        // CLI accepts, which is the same class of defect as the prose
        // scraping it replaced.
        guard let r = try? await runner.run("egg-config", Commands.mapMachine())
        else { return }
        let t = Commands.parseButtonTables(r.text)
        buttons = t.buttons
        actions = t.actions
        actionArg = t.actionArg
        actionCategory = t.actionCategory
        keyNames = t.keyNames
    }
}

// ---------------------------------------------------------------- the screen
//
// LAID OUT LIKE THE VENDOR'S, deliberately, and the reason is not flattery.
//
// This screen used to be a five-way segmented control -- One field / Buttons /
// CPI / Click filter / Handedness -- whose first panel was "pick a field from
// a menu, then pick a value". That is the CLI's shape, not a settings page:
// it asks the user to know the name of the thing they want before they can
// see it. The device's owner called it confusing, and was right.
//
// Endgame's own tool is four tabs -- Basic, Advanced Sensor, Buttons, Button
// Mapping -- with every setting on the page as its own labelled row. Anyone
// who has used the Windows tool already knows where things are, and anyone who
// has not can read the whole page instead of opening a menu to find out what
// is in it. So the tabs carry the vendor's names and each field sits on the
// tab the vendor puts it on.
//
// WHAT IS DELIBERATELY NOT COPIED, because copying it would be a safety
// regression:
//
//  - Their single APPLY at the bottom writes the whole page at once. This tool
//    writes ONE field at a time, read-modify-write, and reads it back
//    (engineering-rules.md §4.1). Hiding that behind one button would imply an
//    atomicity the protocol does not have. So there is one pending change at a
//    time, named in a bar at the bottom, with its own Preview and Apply.
//  - Their "Disable LED on Lift-Off" checkbox is the INVERSE of the byte. The
//    row here is named after the byte, as the CLI's field is, and says so.
//  - Their Advanced Sensor page hides the motion jitter filter. It is shown
//    here, with the CLI's own sentence about why nobody has ever set it.
//
// Every control is built from `egg-config set`'s table -- name, accepts,
// citation -- and every value from `egg-config show --machine`. The app holds
// no list of settings of its own.

struct ConfigView: View {
    @Binding var screen: Screen
    @EnvironmentObject var runner: ToolRunner
    @StateObject private var model = ConfigModel()

    /// The vendor's four pages, by their captions.
    enum Tab: String, CaseIterable, Identifiable {
        case basic = "Basic", sensor = "Advanced Sensor"
        case buttons = "Buttons", mapping = "Button Mapping"
        var id: String { rawValue }
    }
    @State private var tab: Tab = .basic

    /// The one change waiting to be written.
    ///
    /// ONE, not a set. `egg-config` writes a single field per invocation and
    /// verifies it; a form that queued five changes and then applied them
    /// would be five separate read-modify-write cycles presented as one act,
    /// and if the third failed the user would have no idea which two had
    /// landed. So changing a second control replaces the first, and the first
    /// control snaps back to what the mouse says -- because every control
    /// reads its value from the device reading unless it is the pending one.
    struct Pending: Equatable {
        var key: String
        var label: String
        var from: String
        var to: String
        var preview: [String]
        var apply: [String]
        /// Set when the typed value is one the CLI would refuse. Preview stays
        /// disabled and this is shown instead, rather than sending a command
        /// whose rejection the user did not cause.
        var problem: String?
    }
    @State private var pending: Pending?
    @State private var previewed = false

    /// The inline argument editor for the two actions that take one. The
    /// vendor pops a dialog for KEYBOARD KEY; this is the same thing without a
    /// window, and it is per-row so it cannot be aimed at the wrong button.
    @State private var argSlot: String?
    @State private var argKind = "key"
    @State private var argText = ""

    @State private var showOutput = false
    @State private var showReading = false

    // The restore flow's two-step state. `restorePath` is set by the file
    // panel and survives the preview so the apply cannot target a different
    // file than the one that was shown.
    @State private var restorePath: String = ""
    @State private var restorePreviewed = false

    /// Vendor captions, so someone coming from the Windows tool finds the row
    /// they are looking for. A field with no entry here shows its CLI name,
    /// which is how a field this table has never heard of still appears.
    private static let captions: [String: String] = [
        "polling": "Polling Rate",
        "lod": "LOD",
        "cpi-levels": "CPI Levels",
        "angle-snapping": "Angle Snapping",
        "led-on-liftoff": "LED on Lift-Off",
        "cpi-stage": "Active CPI stage",
        "cpi-downshift": "CPI Downshift Tuning",
        "smoothing": "Smoothing Tuning",
        "motion-sync": "Motion Sync",
        "force-max-fps": "Force max Sensor fps",
        "sensor-angle": "Sensor Angle Tuning",
        "motion-jitter-filter": "Motion Jitter Filter",
        "slamclick-filter": "Slamclick Filter",
    ]

    /// Which tab each field sits on, following the vendor's pages.
    ///
    /// A field named in NONE of these lists is not dropped -- it appears at the
    /// bottom of Basic under "Not placed on a page yet". engineering-rules.md
    /// §1.2a: leaving a settable field off the screen because a table here has
    /// not been updated is an absence presented as a claim, and this is the
    /// kind of table that goes stale the moment the CLI gains a field.
    private static let basicFields = ["polling", "lod", "cpi-levels",
                                      "angle-snapping", "led-on-liftoff"]
    private static let sensorFields = ["cpi-downshift", "smoothing",
                                       "motion-sync", "force-max-fps",
                                       "sensor-angle", "motion-jitter-filter"]
    private static let buttonFields = ["slamclick-filter"]
    /// Rendered by hand rather than as a row: the active stage is a radio
    /// beside the four CPI rows on the vendor's Basic page, not a dropdown.
    private static let handPlaced = ["cpi-stage"]

    private func caption(_ name: String) -> String {
        Self.captions[name] ?? name
    }

    // ------------------------------------------------------------------ body

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            ScreenHeader(title: "Settings",
                         subtitle: "Every change is previewed, written one "
                                 + "field at a time, and read back.",
                         screen: $screen)

            if let e = model.loadError {
                Text(e).foregroundStyle(.red).padding(.horizontal)
                    .fixedSize(horizontal: false, vertical: true)
            }

            toolbar
            if !restorePath.isEmpty { restoreBar }
            Divider()

            ScrollView {
                VStack(alignment: .leading, spacing: 14) {
                    Picker("", selection: $tab) {
                        ForEach(Tab.allCases) { Text($0.rawValue).tag($0) }
                    }
                    .pickerStyle(.segmented)
                    .labelsHidden()
                    .frame(maxWidth: 470)

                    readingBanner

                    switch tab {
                    case .basic:   basicTab
                    case .sensor:  sensorTab
                    case .buttons: buttonsTab
                    case .mapping: mappingTab
                    }

                    withheldList
                    rawReading
                    outputDisclosure
                }
                .padding(.horizontal)
                .padding(.bottom, 8)
                .frame(maxWidth: .infinity, alignment: .leading)
                .disabled(model.busy)
            }

            pendingBar
        }
        .task {
            await model.loadFields(runner)
            await model.loadButtons(runner)
            // ONE A1 12 READ, WHEN THE SCREEN OPENS. This used to wait for a
            // button, on the rule that the screen sends no frame nobody asked
            // for -- which was right when the screen was a command builder and
            // wrong now that it is a settings page. Every control below shows
            // what the mouse is set to; without this they show nothing, and a
            // form full of blanks is the confusion this redesign is fixing.
            //
            // §4.2a's gate, answered: it answers "what is this mouse set to",
            // which is the screen's whole subject; it cannot be answered off
            // the device; A1 12 is [O], read-only, and the exact exchange the
            // Read button already performed on demand. Opening Settings is
            // asking.
            await model.loadCurrent(runner)
        }
    }

    // --------------------------------------------------------------- toolbar

    @ViewBuilder private var toolbar: some View {
        HStack(spacing: 10) {
            Button("Read again") { Task { await model.loadCurrent(runner) } }
                .help("One A1 12 read, decoded into words. The same exchange "
                    + "`Read from mouse` performs -- nothing extra goes out.")
            Button("Save a copy\u{2026}") { chooseSaveTarget() }
                .help("Reads the record and writes it wherever you say.")
            if FileManager.default.fileExists(
                    atPath: Commands.knownGoodVaultPath()) {
                Button("Restore known-good") {
                    restorePath = Commands.knownGoodVaultPath()
                    restorePreviewed = false
                    run(Commands.previewRestore(record: restorePath)) {
                        restorePreviewed = true
                    }
                }
                .help("The copy egg-config saved by itself the first time it "
                    + "read this mouse (\(Commands.knownGoodVaultName)). It is "
                    + "never overwritten, so it is the oldest good state there "
                    + "is.")
            }
            Menu("More") {
                Button("Read from mouse (raw record)") {
                    showOutput = true
                    run(Commands.readSettings())
                }
                Button("Device info") {
                    showOutput = true
                    run(Commands.deviceInfo())
                }
                Button("Restore a copy\u{2026}") { chooseRestore() }
                Button("Decode a saved record\u{2026}") { chooseShowFile() }
            }
            .frame(width: 90)
            Spacer()
            Button(role: .destructive) {
                showOutput = true
                run(Commands.factoryReset()) {
                    Task { await model.loadCurrent(runner) }
                }
            } label: { Text("Factory reset") }
                .help("A1 13. The device composes its own defaults; this tool "
                    + "sends no settings bytes. Your saved copy is the way "
                    + "back.")
        }
        .padding(.horizontal)
        .disabled(model.busy)
    }

    @ViewBuilder private var restoreBar: some View {
        HStack(spacing: 10) {
            Text("Restore ")
                + Text(URL(fileURLWithPath: restorePath).lastPathComponent)
                    .bold()
            Spacer()
            Button("Write it to the mouse") {
                run(Commands.applyRestore(record: restorePath)) {
                    restorePath = ""; restorePreviewed = false
                    Task { await model.loadCurrent(runner) }
                }
            }
            .disabled(!restorePreviewed)
            .help(restorePreviewed
                  ? "Sends one A0 11, then reads the record back and verifies "
                  + "it byte for byte."
                  : "The preview did not succeed, so there is nothing to "
                  + "approve.")
            Button("Cancel") { restorePath = ""; restorePreviewed = false }
        }
        .padding(.horizontal)
        .padding(.vertical, 6)
        .background(Color.secondary.opacity(0.12))
        .disabled(model.busy)
    }

    /// Where the values on this page came from, and whether they are still true.
    @ViewBuilder private var readingBanner: some View {
        if let e = model.currentError {
            // FIRST LINE, THEN A DISCLOSURE. egg-config's permission
            // diagnostic is twenty lines of what to click and how to confirm
            // it worked, which is the right thing for a terminal and wrong
            // above a form -- it pushed every control off the screen. The
            // whole text is still here, one click away, unedited.
            let lines = e.split(separator: "\n",
                                omittingEmptySubsequences: false).map(String.init)
            let rest = lines.dropFirst().joined(separator: "\n")
            VStack(alignment: .leading, spacing: 2) {
                Text(lines.first ?? e).foregroundStyle(.red)
                    .fixedSize(horizontal: false, vertical: true)
                // macOS will not hand over the 0xFF01 vendor collection
                // without Input Monitoring, and the CLI's advice for that is
                // written for a terminal ("grant it to the app that owns your
                // terminal window"), which is the wrong instruction inside a
                // GUI. Here the app IS the responsible process, so say that
                // and open the pane.
                if e.contains("Input Monitoring") {
                    HStack(spacing: 8) {
                        Text("Grant Input Monitoring to EGG Mouse, then quit "
                           + "and reopen it.")
                            .font(.caption).bold()
                        Button("Open Privacy settings") {
                            if let u = URL(string: "x-apple.systempreferences:"
                                         + "com.apple.preference.security"
                                         + "?Privacy_ListenEvent") {
                                NSWorkspace.shared.open(u)
                            }
                        }
                        .font(.caption)
                    }
                    .fixedSize(horizontal: false, vertical: true)
                }
                if !rest.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
                    DisclosureGroup("What egg-config said, in full") {
                        Text(rest)
                            .font(.system(size: 11, design: .monospaced))
                            .foregroundStyle(.secondary)
                            .fixedSize(horizontal: false, vertical: true)
                            .frame(maxWidth: .infinity, alignment: .leading)
                    }
                    .font(.caption)
                }
            }
        } else if model.current == nil {
            Text("Not read yet, so the controls below have nothing to show. "
               + "`Read again` sends one A1 12.")
                .font(.caption).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
        } else {
            HStack(spacing: 8) {
                Text("Showing what was read from \(model.currentFrom).")
                    .font(.caption).foregroundStyle(.secondary)
                if model.currentStale {
                    Text("A write has succeeded since; these are older than "
                       + "the mouse.")
                        .font(.caption).bold()
                }
            }
            .fixedSize(horizontal: false, vertical: true)
        }
    }

    // ------------------------------------------------------------------ tabs

    @ViewBuilder private var basicTab: some View {
        VStack(alignment: .leading, spacing: 10) {
            ForEach(Self.basicFields, id: \.self) { settingRow($0) }
            Divider().padding(.vertical, 4)
            cpiSection
            unplacedSection
        }
    }

    @ViewBuilder private var sensorTab: some View {
        VStack(alignment: .leading, spacing: 10) {
            ForEach(Self.sensorFields, id: \.self) { settingRow($0) }
        }
    }

    @ViewBuilder private var buttonsTab: some View {
        VStack(alignment: .leading, spacing: 10) {
            ForEach(Self.buttonFields, id: \.self) { settingRow($0) }
            Divider().padding(.vertical, 4)
            Text("Multiclick filter / SPDT").font(.headline)
            ForEach(Commands.multiclickButtons, id: \.self) { multiclickRow($0) }
            Text("The filter value and the SPDT mode SHARE one byte, so a "
               + "button can hold either but not both. Only left and right "
               + "have an SPDT combo behind them, which is why the others "
               + "offer a number and nothing else.")
                .font(.caption).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
            Text("derived: config-protocol.md §7.22, cfg107 0x405f84 / 0x406645")
                .font(.system(size: 10, design: .monospaced))
                .foregroundStyle(.tertiary)
        }
    }

    @ViewBuilder private var mappingTab: some View {
        VStack(alignment: .leading, spacing: 10) {
            handednessRow
            Divider().padding(.vertical, 4)
            if model.buttons.isEmpty {
                Text("egg-config listed no rebindable buttons.")
                    .font(.caption).foregroundStyle(.secondary)
            }
            ForEach(model.buttons, id: \.self) { mappingRow($0) }
            Text("`left` and `cpi-button` are not offered: a mouse whose left "
               + "click has been rebound cannot easily be put back, and the "
               + "CPI button is the way out of a bad CPI.")
                .font(.caption).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    /// Any settable field none of the tab lists names. See `basicFields`.
    @ViewBuilder private var unplacedSection: some View {
        let placed = Set(Self.basicFields + Self.sensorFields
                         + Self.buttonFields + Self.handPlaced)
        let rest = model.fields.map(\.name).filter { !placed.contains($0) }
        if !rest.isEmpty {
            Divider().padding(.vertical, 4)
            Text("Not placed on a page yet").font(.headline)
            Text("`egg-config set` offers these and this screen has no vendor "
               + "page to put them on. Shown here rather than hidden.")
                .font(.caption).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
            ForEach(rest, id: \.self) { settingRow($0) }
        }
    }

    // ------------------------------------------------------------- one row

    /// Caption on the left, control on the right, the CLI's own sentence
    /// underneath. The vendor's four pages are laid out exactly like this.
    @ViewBuilder
    private func labelled<C: View>(_ text: String, note: String? = nil,
                                   gate: String? = nil,
                                   help: String = "",
                                   @ViewBuilder control: () -> C) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            HStack(alignment: .firstTextBaseline, spacing: 12) {
                Text(text)
                    .frame(width: 180, alignment: .trailing)
                    .foregroundStyle(gate == nil ? Color.primary : Color.secondary)
                control().disabled(gate != nil)
                Spacer(minLength: 0)
            }
            if let g = gate {
                // §7.25. Say it here, on the row, rather than letting someone
                // set the control and meet the refusal afterwards.
                Text("This device will not accept a write here: \(g)")
                    .font(.caption).bold()
                    .padding(.leading, 192)
                    .fixedSize(horizontal: false, vertical: true)
            } else if let n = note {
                Text(n)
                    .font(.caption).foregroundStyle(.secondary)
                    .padding(.leading, 192)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
        .help(help)
    }

    /// One row of `egg-config set`'s table, as a control.
    @ViewBuilder private func settingRow(_ name: String) -> some View {
        if let f = model.fields.first(where: { $0.name == name }) {
            labelled(caption(name),
                     // The CLI writes "0 or 1" for a plain flag and adds an
                     // explanation after "--" when there is one worth having.
                     // Showing only the second kind keeps the page readable
                     // without deciding for ourselves what is worth saying.
                     note: f.accepts.contains("--") ? f.accepts : nil,
                     gate: model.gates[name],
                     help: f.record + "\n" + f.cite) {
                control(for: f)
            }
        }
    }

    @ViewBuilder private func control(for f: Field) -> some View {
        let bound = fieldBinding(f.name)
        if f.accepts.hasPrefix("0 or 1") {
            Toggle("", isOn: boolBinding(bound)).labelsHidden()
        } else if let choices = f.choices {
            Picker("", selection: bound) {
                ForEach(choices, id: \.self) {
                    Text(choiceLabel(f, $0)).tag($0)
                }
            }
            .labelsHidden().frame(width: 170)
        } else if let r = Commands.range(for: f.accepts), r.count <= 12 {
            Picker("", selection: bound) {
                ForEach(Array(r), id: \.self) {
                    Text(choiceLabel(f, String($0))).tag(String($0))
                }
            }
            .labelsHidden().frame(width: 170)
        } else if let r = Commands.range(for: f.accepts) {
            // The vendor's Sensor Angle Tuning is a slider with the number
            // beside it; so is this.
            HStack(spacing: 8) {
                // No `step:`. A stepped Slider draws a tick per step, and
                // sensor-angle has 255 of them -- the control came out as a
                // grey comb. Rounding happens in the binding instead, so the
                // value is still an integer.
                Slider(value: sliderBinding(bound, in: r),
                       in: Double(r.lowerBound)...Double(r.upperBound))
                    .frame(width: 200)
                TextField("", text: bound).frame(width: 64)
            }
        } else {
            TextField("", text: bound).frame(width: 170)
        }
    }

    /// What a value is called on screen.
    ///
    /// `lod` is the case that matters: the vendor's dropdown reads "1.0mm" and
    /// a dropdown reading 0..10 would be worse than either. The millimetres are
    /// READ out of the CLI's own `accepts` sentence by Commands.rangeLabels,
    /// not written down again here -- if that wording ever changes the labels
    /// fall back to bare numbers rather than to wrong ones.
    private func choiceLabel(_ f: Field, _ v: String) -> String {
        if let labels = Commands.rangeLabels(for: f.accepts),
           let n = Int(v), let mm = labels[n] { return mm }
        if let u = Commands.unitSuffix(for: f.accepts) { return v + " " + u }
        return v
    }

    // -------------------------------------------------------------- bindings

    /// What the mouse says this field is, as a value `set` would take.
    private func deviceValue(_ name: String) -> String {
        guard let f = model.current?.fields.first(where: { $0.name == name })
        else { return "" }
        return Commands.settableValue(of: f)
    }

    /// A control's value: the pending change if this row owns it, otherwise
    /// whatever the mouse said. That is what makes "one pending change at a
    /// time" work without any bookkeeping -- the moment a different row takes
    /// the pending slot, this one is reading the device again.
    private func fieldBinding(_ name: String) -> Binding<String> {
        Binding(
            get: {
                if let p = pending, p.key == "field:" + name { return p.to }
                return deviceValue(name)
            },
            set: { v in
                previewed = false
                let from = deviceValue(name)
                guard v != from, !v.isEmpty else { pending = nil; return }
                pending = Pending(
                    key: "field:" + name,
                    label: caption(name),
                    from: from, to: v,
                    preview: Commands.previewSet(field: name, value: v),
                    apply: Commands.applySet(field: name, value: v),
                    problem: nil)
            })
    }

    private func boolBinding(_ s: Binding<String>) -> Binding<Bool> {
        Binding(get: { s.wrappedValue == "1" },
                set: { s.wrappedValue = $0 ? "1" : "0" })
    }

    private func sliderBinding(_ s: Binding<String>,
                               in r: ClosedRange<Int>) -> Binding<Double> {
        Binding(
            get: { Double(Int(s.wrappedValue) ?? r.lowerBound) },
            set: { s.wrappedValue = String(Int($0.rounded())) })
    }

    // ----------------------------------------------------------- CPI stages

    /// The vendor's Basic page: four CPI rows, a radio marking the active one,
    /// and X/Y boxes. `cpi-levels` above decides how many are selectable, so a
    /// stage past it is shown greyed with the CLI's own word for it.
    @ViewBuilder private var cpiSection: some View {
        Text("CPI stages").font(.headline)
        if let c = model.current, !c.cpi.isEmpty {
            ForEach(c.cpi, id: \.index) { s in cpiRow(s) }
        } else {
            Text("Not read yet.").font(.caption).foregroundStyle(.secondary)
        }
        Text("10 to 10000 in steps of 10, then 10050 to 30000 in steps of 50. "
           + "The X\u{2260}Y flag byte is computed, never typed.")
            .font(.caption).foregroundStyle(.secondary)
            .padding(.leading, 192)
            .fixedSize(horizontal: false, vertical: true)
        // Shown only when it applies, which is the one case the vendor's tool
        // gets wrong. Carried over from the old CPI panel deliberately: this
        // was a redesign of the layout, not a place to quietly drop a caveat.
        if let p = pending, p.key == "cpi:4", p.to.contains("x") {
            Text("Note: for stage 4 only, the vendor's own tool reads the "
               + "X\u{2260}Y flag off stage 3's boxes (a bug at 0x40edde). This "
               + "tool computes it from stage 4, so that one byte can differ "
               + "from what Windows would send. No observation of stage 4 with "
               + "X\u{2260}Y exists either way.")
                .font(.caption).foregroundStyle(.orange)
                .padding(.leading, 192)
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    @ViewBuilder private func cpiRow(_ s: Commands.Shown.Stage) -> some View {
        HStack(alignment: .firstTextBaseline, spacing: 12) {
            // The active-stage radio. `cpi-stage` counts from 0 and the CLI
            // says so in its own accepts line ("0 to 3 -- which CPI stage is
            // ACTIVE"), which is the only place that +1 comes from.
            Toggle("", isOn: activeStageBinding(s.index))
                .toggleStyle(.checkbox)
                .labelsHidden()
                .disabled(model.fields.first { $0.name == "cpi-stage" } == nil
                          || model.gates["cpi-stage"] != nil)
            Text("CPI \(s.index)").frame(width: 60, alignment: .leading)
            TextField("X", text: cpiBinding(s, axis: .x)).frame(width: 74)
            TextField("Y", text: cpiBinding(s, axis: .y)).frame(width: 74)
            if !s.inUse {
                Text("beyond CPI Levels; not selectable")
                    .font(.caption).foregroundStyle(.secondary)
            }
            Spacer(minLength: 0)
        }
    }

    private enum Axis { case x, y }

    /// One CPI stage's X or Y. Both axes go into ONE pending change, because
    /// `egg-config cpi` writes the pair -- offering them as two pending changes
    /// would let someone approve an X that is then sent alongside an
    /// unapproved Y.
    private func cpiBinding(_ s: Commands.Shown.Stage,
                            axis: Axis) -> Binding<String> {
        let key = "cpi:\(s.index)"
        return Binding(
            get: {
                if let p = pending, p.key == key {
                    let parts = p.to.split(separator: "x").map(String.init)
                    if axis == .x { return parts.first ?? "" }
                    return parts.count > 1 ? parts[1] : (parts.first ?? "")
                }
                return String(axis == .x ? s.x : s.y)
            },
            set: { v in
                previewed = false
                let x = axis == .x ? v : cpiBinding(s, axis: .x).wrappedValue
                let y = axis == .y ? v : cpiBinding(s, axis: .y).wrappedValue
                setPendingCpi(stage: s.index, was: (s.x, s.y), x: x, y: y)
            })
    }

    private func setPendingCpi(stage: Int, was: (x: Int, y: Int),
                               x: String, y: String) {
        let key = "cpi:\(stage)"
        guard let xi = Int(x.trimmingCharacters(in: .whitespaces)),
              let yi = Int(y.trimmingCharacters(in: .whitespaces)) else {
            pending = Pending(key: key, label: "CPI \(stage)",
                              from: "\(was.x)x\(was.y)", to: "\(x)x\(y)",
                              preview: [], apply: [],
                              problem: "Both boxes have to be numbers.")
            return
        }
        if xi == was.x && yi == was.y { pending = nil; return }
        // Shown rather than silently corrected: the user typed a number and is
        // entitled to know where it lands (§1.3's spirit -- nothing changes
        // behind the user's back, not even a number in a box).
        var problem: String?
        if !Commands.cpiIsLegal(xi) {
            problem = "\(xi) is off the grid; the vendor's tool would store "
                    + "\(Commands.normaliseCpi(xi))."
        } else if !Commands.cpiIsLegal(yi) {
            problem = "\(yi) is off the grid; the vendor's tool would store "
                    + "\(Commands.normaliseCpi(yi))."
        }
        pending = Pending(
            key: key, label: "CPI \(stage)",
            from: was.x == was.y ? "\(was.x)" : "\(was.x)x\(was.y)",
            to: xi == yi ? "\(xi)" : "\(xi)x\(yi)",
            preview: Commands.previewCpi(stage: stage, x: xi, y: yi),
            apply: Commands.applyCpi(stage: stage, x: xi, y: yi),
            problem: problem)
    }

    /// The radio beside a CPI row. Ticking it sets `cpi-stage`; there is no
    /// untick, because some stage is always the active one.
    private func activeStageBinding(_ stage: Int) -> Binding<Bool> {
        let s = fieldBinding("cpi-stage")
        return Binding(
            get: { s.wrappedValue == String(stage - 1) },
            set: { on in if on { s.wrappedValue = String(stage - 1) } })
    }

    // ------------------------------------------------- multiclick and SPDT

    private func mcDevice(_ button: String) -> (mode: String, value: String) {
        guard let c = model.current?.clicks.first(where: { $0.button == button })
        else { return ("off", "") }
        return (c.mode, String(c.value))
    }

    private func mcState(_ button: String) -> (mode: String, value: String) {
        if let p = pending, p.key == "mc:" + button {
            let parts = p.to.split(separator: " ").map(String.init)
            return (parts.first ?? "off", parts.count > 1 ? parts[1] : "")
        }
        return mcDevice(button)
    }

    private func setPendingMulticlick(_ button: String,
                                      mode: String, value: String) {
        previewed = false
        let was = mcDevice(button)
        let to = mode == "off" ? "off " + value : mode
        let from = was.mode == "off" ? "off " + was.value : was.mode
        guard to != from else { pending = nil; return }
        let v = Int(value.trimmingCharacters(in: .whitespaces))
        let legal = Commands.multiclickIsLegal(mode: mode, value: v,
                                               button: button)
        pending = Pending(
            key: "mc:" + button,
            label: "Multiclick filter, \(button)",
            from: from, to: to,
            preview: legal ? Commands.previewMulticlick(button: button,
                                                        mode: mode, value: v)
                           : [],
            apply: legal ? Commands.applyMulticlick(button: button,
                                                    mode: mode, value: v)
                         : [],
            problem: legal ? nil
                           : "A filter value is 0 to 25, and only left and "
                           + "right have an SPDT combo to put in GX mode.")
    }

    @ViewBuilder private func multiclickRow(_ button: String) -> some View {
        let state = mcState(button)
        let modes = Commands.multiclickModes(for: button)
        let unknown = mcDevice(button).mode == "unknown"
        labelled(button.prefix(1).uppercased() + button.dropFirst() + " Button",
                 note: unknown
                     ? "The byte here is neither a filter value nor a GX mode, "
                     + "so this tool will not guess what it means."
                     : nil,
                 help: "record 0x3d + 7n, shared with the SPDT mode") {
            HStack(spacing: 8) {
                // The three buttons with no SPDT combo still leave the
                // picker's width empty, so all five value boxes line up the
                // way the vendor's five sliders do.
                if modes.count == 1 { Color.clear.frame(width: 120, height: 1) }
                if modes.count > 1 {
                    Picker("", selection: Binding(
                        get: { state.mode },
                        set: { setPendingMulticlick(button, mode: $0,
                                                    value: state.value) })) {
                        ForEach(modes, id: \.self) { Text($0).tag($0) }
                    }
                    .labelsHidden().frame(width: 120)
                }
                if state.mode == "off" {
                    TextField("0-25", text: Binding(
                        get: { state.value },
                        set: { setPendingMulticlick(button, mode: "off",
                                                    value: $0) }))
                        .frame(width: 64)
                }
            }
            .disabled(unknown)
        }
    }

    // ------------------------------------------------------------ handedness

    @ViewBuilder private var handednessRow: some View {
        let handed = model.current?.handed ?? ""
        labelled("Left-handed Mode",
                 note: handed == "unknown"
                     ? "UNKNOWN \u{2014} neither entry holds the primary-click "
                     + "value, a state the vendor's own UI cannot produce. The "
                     + "CLI refuses to guess, so this is not offered."
                     : "Not a flag byte. The vendor implements this by MOVING "
                     + "your button assignment between the left and right "
                     + "entries, so Preview reads the mouse first and shows "
                     + "exactly which bytes would change.",
                 help: "derived: config-protocol.md §7.20, cfg107 0x408b00") {
            Toggle("", isOn: handednessBinding)
                .labelsHidden()
                .disabled(handed.isEmpty || handed == "unknown")
        }
    }

    private var handednessBinding: Binding<Bool> {
        Binding(
            get: {
                if let p = pending, p.key == "handedness" { return p.to == "left" }
                return model.current?.handed == "left"
            },
            set: { on in
                previewed = false
                let side = on ? "left" : "right"
                let was = model.current?.handed ?? ""
                guard side != was else { pending = nil; return }
                pending = Pending(
                    key: "handedness", label: "Handedness",
                    from: was, to: side,
                    preview: Commands.previewHandedness(side),
                    apply: Commands.applyHandedness(side),
                    problem: nil)
            })
    }

    // --------------------------------------------------------- button mapping

    /// The vendor's menu is two levels deep, and so is this one.
    ///
    /// One flat list of nineteen actions was what this screen had, and it is
    /// the specific thing the device's owner asked to change: MEDIA alone is
    /// eight entries, so the mouse actions a person actually wants are eight
    /// rows further down than they need to be. The groups are not invented
    /// here -- `egg-config map --machine` files every action under one, and
    /// Commands.actionCategoryOrder only decides what order they appear in.
    @ViewBuilder private func mappingRow(_ slot: String) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            HStack(alignment: .firstTextBaseline, spacing: 12) {
                Text(slot.prefix(1).uppercased() + slot.dropFirst())
                    .frame(width: 180, alignment: .trailing)
                Menu(mappingLabel(slot)) {
                    ForEach(orderedCategories, id: \.self) { cat in
                        categoryMenu(slot, cat)
                    }
                }
                .frame(width: 220)
                Spacer(minLength: 0)
            }
            if argSlot == slot { argEditor(slot) }
        }
    }

    /// What the button is bound to now, or the change waiting for approval.
    private func mappingLabel(_ slot: String) -> String {
        if let p = pending, p.key == "map:" + slot { return pretty(p.to) }
        guard let b = model.current?.buttons.first(where: { $0.slot == slot })
        else { return "\u{2014}" }
        if b.unknown { return "UNKNOWN" }
        return pretty(b.action) + (b.detail.isEmpty ? "" : " " + b.detail)
    }

    /// Groups in the vendor's order, then anything the CLI emits that this
    /// build has never heard of -- appended rather than dropped.
    private var orderedCategories: [String] {
        let present = model.actions.compactMap { model.actionCategory[$0] }
        var out = Commands.actionCategoryOrder.filter { present.contains($0) }
        for c in present where !out.contains(c) { out.append(c) }
        return out
    }

    @ViewBuilder private func categoryMenu(_ slot: String,
                                           _ cat: String) -> some View {
        let members = model.actions.filter { model.actionCategory[$0] == cat }
        if members.count == 1 {
            // A group of one is a plain item, never a submenu of one. The
            // vendor's menu has two: DISABLE binds straight away, KEYBOARD KEY
            // asks for the key first. Ours does the same because it is the
            // same two groups -- `disable` and `keyboard` each hold exactly
            // one action in the CLI's table.
            let only = members[0]
            if model.actionArg[only] == "none" {
                Button(categoryLabel(cat)) { setPendingMap(slot, only) }
            } else {
                Button(categoryLabel(cat) + "\u{2026}") {
                    argSlot = slot
                    argKind = model.actionArg[only] ?? "none"
                    argText = ""
                }
            }
        } else {
            Menu(categoryLabel(cat)) {
                ForEach(members, id: \.self) { a in
                    if model.actionArg[a] == "none" {
                        Button(pretty(a)) { setPendingMap(slot, a) }
                    } else {
                        // fixed-cpi and key take an argument, and `egg-config
                        // map right fixed-cpi` is a refusal. Opening an editor
                        // is the vendor's behaviour too: KEYBOARD KEY pops a
                        // dialog rather than binding anything.
                        Button(pretty(a) + "\u{2026}") {
                            argSlot = slot
                            argKind = model.actionArg[a] ?? "none"
                            argText = ""
                        }
                    }
                }
            }
        }
    }

    private func categoryLabel(_ c: String) -> String {
        switch c {
        case "cpi":      return "CPI"
        case "keyboard": return "Keyboard key"
        default:         return c.prefix(1).uppercased() + c.dropFirst()
        }
    }

    /// Display only. The CLI's name is what gets sent; this just makes
    /// `right-click` read as `Right click` in a menu.
    private func pretty(_ a: String) -> String {
        let s = a.replacingOccurrences(of: "-", with: " ")
        return s.prefix(1).uppercased() + s.dropFirst()
    }

    @ViewBuilder private func argEditor(_ slot: String) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack(spacing: 8) {
                TextField(argKind == "cpi" ? "e.g. 1600 or 1600x800"
                                           : "e.g. a  ctrl+shift+a  f5",
                          text: $argText)
                    .frame(width: 220)
                Button("Use this") {
                    let a = argText.trimmingCharacters(in: .whitespaces)
                    guard !a.isEmpty else { return }
                    setPendingMap(slot, (argKind == "cpi" ? "fixed-cpi" : "key")
                                        + ":" + a)
                    argSlot = nil
                }
                .disabled(argText.trimmingCharacters(in: .whitespaces).isEmpty)
                Button("Cancel") { argSlot = nil }
            }
            Text(argKind == "cpi"
                 ? "10 to 30000 in steps of 10. One number for both axes, or "
                 + "XxY for independent ones."
                 : "Modifiers ctrl, shift, alt and gui combine with +. Named "
                 + "keys: " + model.keyNames.prefix(14).joined(separator: " ")
                 + (model.keyNames.count > 14 ? " \u{2026}" : ""))
                .font(.caption).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
        }
        .padding(.leading, 192)
    }

    private func setPendingMap(_ slot: String, _ action: String) {
        previewed = false
        let b = model.current?.buttons.first(where: { $0.slot == slot })
        let was = b.map { $0.unknown ? "UNKNOWN"
                                     : $0.action
                                       + ($0.detail.isEmpty ? "" : ":" + $0.detail) }
                  ?? ""
        guard action != was else { pending = nil; return }
        pending = Pending(
            key: "map:" + slot, label: "Button \(slot)",
            from: pretty(was), to: action,
            preview: Commands.previewMap(button: slot, action: action),
            apply: Commands.applyMap(button: slot, action: action),
            problem: nil)
    }

    // ------------------------------------------------------- the pending bar

    /// One change, named, with the dry run in front of the write.
    ///
    /// The vendor's page has a single APPLY that writes everything at once.
    /// This does not, and the difference is the point: `egg-config` writes one
    /// field, reads it back and verifies it, so a bar that said APPLY over five
    /// queued edits would be claiming an atomicity the protocol has not got.
    /// What is on screen is exactly what the next command will do.
    @ViewBuilder private var pendingBar: some View {
        if let p = pending {
            VStack(spacing: 0) {
                Divider()
                HStack(alignment: .firstTextBaseline, spacing: 12) {
                    VStack(alignment: .leading, spacing: 1) {
                        Text(p.label).bold()
                        Text(p.from.isEmpty ? p.to
                                            : p.from + "  \u{2192}  " + p.to)
                            .font(.caption).foregroundStyle(.secondary)
                        if let why = p.problem {
                            Text(why).font(.caption).foregroundStyle(.orange)
                                .fixedSize(horizontal: false, vertical: true)
                        }
                    }
                    Spacer()
                    Button("Preview") {
                        previewed = true
                        showOutput = true
                        run(p.preview)
                    }
                    .disabled(p.problem != nil || p.preview.isEmpty)
                    .help("A dry run. Prints the exact byte that would move, "
                        + "and sends nothing.")
                    Button("Apply") {
                        run(p.apply) {
                            pending = nil
                            previewed = false
                            // The CLI has already read this back and verified
                            // it; this re-read is so the CONTROLS stop showing
                            // the old value. A settings page that lies about
                            // the current state after a successful write is
                            // worse than one extra A1 12.
                            Task { await model.loadCurrent(runner) }
                        }
                    }
                    .disabled(!previewed || p.problem != nil || p.apply.isEmpty)
                    .help(previewed ? "Write it, read it back, verify."
                                    : "Preview it first.")
                    Button("Discard") { pending = nil; previewed = false }
                }
                .padding(.horizontal)
                .padding(.vertical, 8)
            }
            .background(Color.secondary.opacity(0.12))
            .disabled(model.busy)
        }
    }

    // ------------------------------------------------------------ disclosures

    /// "Why is glass-mode not in the list?"
    ///
    /// The CLI prints a paragraph for each field it derived and deliberately
    /// does NOT offer, and the app has parsed those into `model.withheld` since
    /// the day it shipped -- and for a while never rendered them. So a user
    /// looking for `glass-mode`, `multiclick-filter` or the vendor's inverted
    /// "Disable LED on Lift-Off" saw no trace of it and no explanation.
    /// engineering-rules.md §1.2a: an absence is a claim, and that rule applies
    /// to our own UI.
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

    /// Everything `show --machine` said, including what the form above cannot
    /// put on a control.
    ///
    /// THE RULE THIS FOLLOWS. It renders what the CLI said and nothing else.
    /// It does not compute a reading, does not fall back to a plausible
    /// default, and does not hide a row it cannot explain: a field whose byte
    /// is `undecodable` is shown in red with the CLI's own sentence, and a
    /// button entry that matched none of the 19 derived actions says UNKNOWN
    /// with its raw bytes beside it. §1.2a -- an absence shown as a blank is a
    /// claim.
    ///
    /// Split into one small view per section because SwiftUI's type checker
    /// gives up on the whole thing in one body. That is a real constraint, not
    /// a style choice, and it is worth a line here so nobody merges them back.
    @ViewBuilder private var rawReading: some View {
        if let c = model.current {
            DisclosureGroup("Everything the mouse reported",
                            isExpanded: $showReading) {
                VStack(alignment: .leading, spacing: 14) {
                    shownFields(c.fields)
                    shownCpi(c.cpi)
                    shownButtons(c)
                    shownClicks(c.clicks)
                    shownRaw(c.raw)
                }
                .padding(.top, 6)
            }
            .font(.caption)
        }
    }

    @ViewBuilder private var outputDisclosure: some View {
        DisclosureGroup("Tool output", isExpanded: $showOutput) {
            OutputPane(text: runner.liveOutput.isEmpty ? model.output
                                                       : runner.liveOutput)
                .frame(height: 220)
                .padding(.top, 6)
        }
        .font(.caption)
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

    // ---------------------------------------------------------------- panels

    private func chooseShowFile() {
        let p = NSOpenPanel()
        p.canChooseFiles = true
        p.allowsMultipleSelection = false
        p.message = "Choose a settings record saved earlier"
        guard p.runModal() == .OK, let u = p.url else { return }
        showReading = true
        Task { await model.loadCurrent(runner, from: u.path) }
    }

    /// Two clicks, never one. The first is offline (`dryrun`) and shows the
    /// whole frame plus the diff; the second is the only one that opens the
    /// device. Deliberately NOT modelled as a confirmation alert: an alert asks
    /// "are you sure" about a thing the user has not been shown, which is the
    /// reflex engineering-rules.md §4.2c exists to refuse. Here the second
    /// click comes after the bytes are on screen.
    private func chooseRestore() {
        let p = NSOpenPanel()
        p.canChooseFiles = true
        p.allowsMultipleSelection = false
        p.message = "Choose a settings record saved earlier"
        guard p.runModal() == .OK, let u = p.url else { return }
        restorePath = u.path
        restorePreviewed = false
        showOutput = true
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
        showOutput = true
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
        guard !args.isEmpty else { return }
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
                // A write that succeeded makes the decoded table older than the
                // mouse. Every write path above follows its `ok` with a re-read,
                // so this is what covers the ones that do not -- and saying so
                // beats a page quietly showing the wrong number.
                if r.ok && args.contains("--yes") && model.current != nil {
                    model.currentStale = true
                }
            } catch {
                showOutput = true
                model.output = error.localizedDescription
            }
        }
    }
}
