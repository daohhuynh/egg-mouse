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
    func loadButtons(_ runner: ToolRunner) async {
        guard let r = try? await runner.run("egg-config", ["map"]) else { return }
        var b: [String] = [], a: [String] = []
        var mode = 0
        for line in r.text.split(separator: "\n") {
            let s = String(line)
            if s.lowercased().contains("button") && s.hasSuffix(":") { mode = 1; continue }
            if s.lowercased().contains("action") && s.hasSuffix(":") { mode = 2; continue }
            let t = s.trimmingCharacters(in: .whitespaces)
            guard s.hasPrefix("  "), !t.isEmpty else { continue }
            let word = String(t.split(separator: " ").first ?? "")
            guard !word.isEmpty else { continue }
            if mode == 1 { b.append(word) } else if mode == 2 { a.append(word) }
        }
        buttons = b
        actions = a
    }
}

struct ConfigView: View {
    @Binding var screen: Screen
    @EnvironmentObject var runner: ToolRunner
    @StateObject private var model = ConfigModel()

    @State private var selected: String = ""
    @State private var value: String = ""
    @State private var previewed = false

    var field: Field? { model.fields.first { $0.name == selected } }

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
