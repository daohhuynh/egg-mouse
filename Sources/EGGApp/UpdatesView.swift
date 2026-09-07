// UpdatesView.swift -- teaching this build about a version it has never seen.
//
// Reads files. Sends nothing to the mouse. That is worth stating at the top,
// because it is the only screen whose job is to be pointed at an unknown
// executable, and the instinct that "analysing a file is risky" would be the
// wrong lesson to draw from the rest of this app.
//
// What it CANNOT do, and says so on screen: adopt anything by itself. The
// ingest tools print a manifest row, or a verdict about a config layout, and a
// person commits it. engineering-rules.md §1.4 requires the firmware resource id to be a
// compile-time constant, and it stays one -- see FirmwareManifest.h for the
// argument that a hash-keyed table preserves that rule.
import SwiftUI

@MainActor
final class UpdatesModel: ObservableObject {
    @Published var path = ""
    @Published var label = ""
    @Published var kind: Kind = .firmware
    @Published var output = ""
    @Published var busy = false

    enum Kind: String, CaseIterable, Identifiable {
        case firmware = "Firmware updater"
        case config = "Configuration tool"
        var id: String { rawValue }
        var script: String {
            self == .firmware ? "Tools/pe/ingest.py" : "Tools/pe/ingest_config.py"
        }
    }
}

struct UpdatesView: View {
    @Binding var screen: Screen
    @EnvironmentObject var runner: ToolRunner
    @StateObject private var m = UpdatesModel()

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            ScreenHeader(title: "New versions",
                         subtitle: "Read an updater or config tool this build "
                                 + "has never seen, and report what can be "
                                 + "established from it.",
                         screen: $screen)

            HStack(alignment: .top, spacing: 16) {
                VStack(alignment: .leading, spacing: 10) {
                    Picker("Kind", selection: $m.kind) {
                        ForEach(UpdatesModel.Kind.allCases) { Text($0.rawValue).tag($0) }
                    }
                    .pickerStyle(.radioGroup)

                    HStack {
                        TextField(".exe to read", text: $m.path)
                        Button("Choose…") { choose() }
                    }

                    if m.kind == .firmware {
                        TextField("Version label, e.g. 1.11", text: $m.label)
                        Text("The label is what you would type as --version. "
                             + "It is not read from the file: Endgame's own "
                             + "version resource says 1.1.0.0 for what they "
                             + "call 1.10 and 1.0.4.0 for 1.04, and a rule "
                             + "fitted to four releases is not a rule. The "
                             + "SHA-256 is what actually identifies the file.")
                            .font(.caption).foregroundStyle(.secondary)
                            .fixedSize(horizontal: false, vertical: true)
                    } else {
                        Text("Every setting this app can change is re-derived "
                             + "from the file you choose — the record it lands "
                             + "on, and the exact set of values the vendor's "
                             + "own code stores. If any one of them fails to "
                             + "re-derive, or lands somewhere different, the "
                             + "answer is a refusal with the details.")
                            .font(.caption).foregroundStyle(.secondary)
                            .fixedSize(horizontal: false, vertical: true)
                    }

                    Button("Read it") { run() }
                        .disabled(m.path.isEmpty || m.busy
                                  || (m.kind == .firmware && m.label.isEmpty))

                    Label("Reads files only. Nothing is sent to the mouse.",
                          systemImage: "lock.shield")
                        .font(.caption).foregroundStyle(.green)

                    // The two halves leave DIFFERENT artefacts, and saying so
                    // is the point: until 2026-09-06 this paragraph promised
                    // "what to paste and where" for both, and the config half
                    // printed a verdict that was recorded nowhere at all.
                    Text(m.kind == .firmware
                         ? "Adopting a result is a source change, on purpose: "
                           + "it is reviewable, and it keeps the firmware "
                           + "resource id a compile-time constant. The output "
                           + "below prints the manifest row to paste into "
                           + "FirmwareManifest.cpp, and the rest of the "
                           + "checklist with it."
                         : "Nothing at run time reads a config-tool verdict, "
                           + "so adopting one is not a source change. The "
                           + "output below prints a row for "
                           + "notes/config-adoption.md instead, so that six "
                           + "months from now somebody can tell whether this "
                           + "version was ever checked. A verdict nobody can "
                           + "look up later is not much better than one "
                           + "nobody computed.")
                        .font(.caption).foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)

                    Spacer()
                }
                .frame(width: 330)

                OutputPane(text: m.output)
            }
            .padding([.horizontal, .bottom])
        }
    }

    private func choose() {
        let p = NSOpenPanel()
        p.canChooseFiles = true
        p.allowsMultipleSelection = false
        p.message = "Choose an Endgame .exe"
        if p.runModal() == .OK, let u = p.url { m.path = u.path }
    }

    /// The repository root, found by walking up from the app until a marker is
    /// seen. The ingest scripts are repo tools and read the checked-in binaries
    /// they compare against, so they need to run there.
    ///
    /// The marker is the script this screen is about to RUN, not a document.
    /// It used to be `engineering-rules.md`, which stopped being committed on 2026-09-07
    /// -- on any fresh clone that file is absent, so the old marker would have
    /// reported "not in a repository" forever while the repository was right
    /// there. A marker should be something the caller depends on anyway, so
    /// that losing it is the same event as the feature not working.
    private static let rootMarker = "Tools/pe/ingest.py"

    private func repoRoot() -> URL? {
        func isRoot(_ d: URL) -> Bool {
            FileManager.default.fileExists(
                atPath: d.appendingPathComponent(Self.rootMarker).path)
        }
        var d = Bundle.main.bundleURL.deletingLastPathComponent()
        for _ in 0..<6 {
            if isRoot(d) { return d }
            d = d.deletingLastPathComponent()
        }
        let cwd = URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
        return isRoot(cwd) ? cwd : nil
    }

    private func run() {
        guard let root = repoRoot() else {
            m.output = """
            This screen runs the repository's ingest scripts, and the \
            repository was not found from where this app is running.

            Open the app from inside the egg-mouse checkout, or run the tool \
            directly:

                python3 \(m.kind.script) <file.exe>\
            \(m.kind == .firmware ? " --label \(m.label)" : "")
            """
            return
        }
        var args = [m.path]
        if m.kind == .firmware { args += ["--label", m.label] }
        Task {
            m.busy = true
            defer { m.busy = false }
            do {
                let r = try await runner.runPython(m.kind.script, args,
                                                   repoRoot: root)
                m.output = "$ python3 \(m.kind.script) "
                         + args.joined(separator: " ") + "\n\n" + r.text
            } catch {
                m.output = error.localizedDescription
            }
        }
    }
}
