#include "cli.h"
#include "dllinjector/text.h"
#include "dllinjector/version.h"
#include <algorithm>

namespace dllinjector::cli {
namespace {
std::string Utf8(const std::wstring& text) {
    auto result = ToUtf8(text);
    if (!result) throw result.error();
    return result.value();
}
const char* Bool(bool value) { return value ? "true" : "false"; }
template<class T> std::string Number(const std::optional<T>& value) {
    return value ? std::to_string(*value) : "null";
}
std::string Visible(const std::string& text, bool multiline) {
    constexpr char hex[] = "0123456789abcdef";
    std::string out;
    for (const unsigned char ch : text) {
        if (multiline && (ch == '\n' || ch == '\t')) out += static_cast<char>(ch);
        else if (ch == '\n') out += "\\n";
        else if (ch == '\r') out += "\\r";
        else if (ch == '\t') out += "\\t";
        else if (ch < 0x20 || ch == 0x7f) { out += "\\x"; out += hex[ch >> 4]; out += hex[ch & 15]; }
        else out += static_cast<char>(ch);
    }
    return out;
}
std::string Html(const std::string& text) {
    std::string out;
    for (const char ch : text) {
        switch (ch) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&#39;"; break;
        default: out += ch;
        }
    }
    return out;
}
std::string Fence(const std::string& text, std::size_t minimum) {
    std::size_t longest = 0, run = 0;
    for (const char ch : text) {
        run = ch == '`' ? run + 1 : 0;
        longest = (std::max)(longest, run);
    }
    return std::string((std::max)(minimum, longest + 1), '`');
}

// Only constant labels enter markup. Every report value is a text node or a code span/block.
class Document {
public:
    explicit Document(DocumentFormat format) : html_(format == DocumentFormat::Html) {
        if (html_) text_ =
            "<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
            "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
            "<meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; style-src 'unsafe-inline'; base-uri 'none'; form-action 'none'\">\n"
            "<title>DLLInjector report</title>\n<style>\n"
            "body{margin:0;background:#f3f5f8;color:#182538;font:16px/1.6 system-ui,sans-serif}"
            "main{max-width:1000px;margin:32px auto;padding:32px;background:white;border-top:5px solid #245da8}"
            "h1{margin-top:0}h2{margin-top:2em;border-bottom:1px solid #d8e0ea;padding-bottom:.3em}"
            "h3{margin-top:1.5em}p{overflow-wrap:anywhere}"
            "code{font-family:Consolas,monospace;white-space:pre-wrap;overflow-wrap:anywhere}"
            "pre{background:#f3f5f8;border:1px solid #d8e0ea;padding:16px;white-space:pre-wrap;overflow-wrap:anywhere}"
            "@media(max-width:600px){main{margin:0;padding:16px}}"
            "@media print{body,main{background:white}main{margin:0;padding:0;max-width:none}h2,h3{break-after:avoid}}\n"
            "</style>\n</head>\n<body>\n<main>\n";
        Heading(1, "DLLInjector report");
    }
    void Heading(int level, const std::string& label) {
        if (html_) text_ += "<h" + std::to_string(level) + ">" + Html(label) + "</h" + std::to_string(level) + ">\n";
        else text_ += std::string(static_cast<std::size_t>(level), '#') + " " + label + "\n\n";
    }
    void Field(const char* label, const std::string& value) {
        const auto visible = Visible(value, false);
        if (html_) text_ += "<p><strong>" + std::string(label) + ":</strong> <code>" + Html(visible) + "</code></p>\n";
        else {
            const auto fence = Fence(visible, 1);
            // Padding separates a delimiter from leading/trailing backticks in the value.
            text_ += "**" + std::string(label) + ":** " + fence + " " + visible + " " + fence + "\n\n";
        }
    }
    void Code(const std::string& value, bool json = false) {
        // The JSON serializer already escapes C0 characters. Preserve its bytes,
        // including DEL, rather than introducing non-JSON escape sequences.
        auto visible = json ? value : Visible(value, true);
        if (visible.empty() || visible.back() != '\n') visible += '\n';
        if (html_) text_ += "<pre><code>" + Html(visible) + "</code></pre>\n";
        else {
            const auto fence = Fence(visible, 3);
            text_ += fence + (json ? "json" : "text") + "\n" + visible + fence + "\n\n";
        }
    }
    std::string Finish() {
        if (html_) text_ += "</main>\n</body>\n</html>\n";
        return std::move(text_);
    }
private:
    bool html_;
    std::string text_;
};

void WriteError(Document& out, const Error& error) {
    out.Field("Category", CategoryName(error.category));
    out.Field("Stage", error.stage);
    out.Field("Message", error.message);
    out.Field("Win32", Number(error.win32));
    out.Field("NTSTATUS", Number(error.ntstatus));
    out.Field("Error verified", Bool(error.verified));
}
void WriteFindings(Document& out, const std::vector<DoctorFinding>& findings) {
    out.Heading(2, "Findings");
    if (findings.empty()) out.Field("Findings", "none; this does not prove loadability");
    for (std::size_t i = 0; i < findings.size(); ++i) {
        const auto& f = findings[i];
        out.Heading(3, "Finding " + std::to_string(i + 1));
        out.Field("Rule", f.rule_id);
        out.Field("Severity", SeverityName(f.severity));
        out.Field("Confidence", f.confidence);
        out.Field("Message", f.message);
        for (const auto& evidence : f.evidence) {
            out.Field("Evidence source", evidence.source);
            out.Field("Evidence", evidence.description);
        }
        out.Field("Recommended next step", f.recommended_next_step);
        for (const auto& url : f.documentation) out.Field("Documentation", url);
    }
}
void WriteDoctor(Document& out, const DoctorReport& doctor) {
    out.Heading(2, "Static image");
    out.Field("Declared target architecture", doctor.target_architecture ? ArchitectureName(*doctor.target_architecture) : "not supplied");
    if (doctor.image) {
        const auto& p = *doctor.image;
        out.Field("Format", p.pe32_plus ? "PE32+" : "PE32");
        out.Field("Machine", std::to_string(p.machine));
        out.Field("Characteristics", std::to_string(p.characteristics));
        out.Field("CLR header present", Bool(p.has_clr_header));
        out.Field("Import library count", std::to_string(p.import_library_count));
        out.Field("Delay import library count", std::to_string(p.delay_import_library_count));
    } else out.Field("Image", "unavailable");
    if (doctor.inspection_error) WriteError(out, *doctor.inspection_error);
    WriteFindings(out, doctor.findings);
    out.Heading(2, "Dependency candidates");
    if (!doctor.dependencies) { out.Field("Observation", "not performed"); return; }
    out.Field("Scope", "explicit_directories");
    out.Field("Filesystem view", "caller_process; includes WOW64 redirection, not loader search order");
    const auto& deps = *doctor.dependencies;
    for (std::size_t i = 0; i < deps.search_directories.size(); ++i) {
        out.Field("Directory index", std::to_string(i));
        out.Field("Directory", Utf8(deps.search_directories[i]));
    }
    if (deps.libraries.empty()) out.Field("Libraries", "none declared in static tables");
    for (std::size_t i = 0; i < deps.libraries.size(); ++i) {
        const auto& lib = deps.libraries[i];
        out.Heading(3, "Dependency " + std::to_string(i + 1));
        out.Field("Name", lib.name);
        out.Field("Kind", lib.delay_import ? "delay_import" : "import");
        out.Field("Status", DependencyStatusName(lib.status));
        for (const auto& candidate : lib.candidates) {
            out.Field("Directory index", std::to_string(candidate.directory_index));
            out.Field("Candidate status", CandidateStatusName(candidate.status));
            if (candidate.error) WriteError(out, *candidate.error);
        }
    }
}
std::string WriteSavedReport(const SavedReport& saved, DocumentFormat format) {
    Document out(format);
    out.Field("Exporter version", ToolVersion);
    out.Field("Export status", "conversion completed");
    out.Field("Interpretation", "This is a supplied snapshot. Schema validation does not verify its origin or facts; source targets were not inspected. Recorded execution flags describe the source command, not this export.");
    out.Field("Source tool version", saved.tool_version);
    out.Field("Source schema version", saved.schema_version);
    out.Field("Source command", saved.command);
    out.Field("Source status", saved.status);
    out.Field("Recorded target PID", saved.target_pid);
    out.Field("Recorded target path", saved.target_path);
    out.Field("Recorded duration (ms)", saved.duration_ms);
    out.Heading(2, "Recorded execution and evidence");
    out.Field("Recorded command accepted", Bool(saved.accepted));
    out.Field("Recorded operation completed", Bool(saved.completed));
    out.Field("Recorded outcome verified", Bool(saved.verified));
    if (saved.evidence.empty()) out.Field("Evidence", "none recorded");
    for (const auto& evidence : saved.evidence) {
        out.Field("Source", evidence.source);
        out.Field("Evidence", evidence.description);
    }
    WriteFindings(out, saved.findings);
    out.Heading(2, "Recorded errors");
    if (saved.errors.empty()) out.Field("Errors", "none recorded");
    for (const auto& error : saved.errors) WriteError(out, error);
    out.Heading(2, "Recorded data");
    out.Code(saved.data_json, true);
    out.Heading(2, "Complete JSON report");
    out.Code(saved.original_json, true);
    return out.Finish();
}
}

Result<std::string> RenderDocument(const Report& report, DocumentFormat format) {
    try {
        if (report.errors.empty()) {
            if (const auto* saved = std::get_if<SavedReport>(&report.data)) return WriteSavedReport(*saved, format);
        }
        const auto json = Render(report, true);
        if (!json) return json.error();
        Document out(format);
        const bool success = report.errors.empty();
        out.Field("Tool version", ToolVersion);
        out.Field("Schema version", SchemaVersion);
        out.Field("Command", report.command);
        out.Field("Status", success ? "success" : "error");
        out.Field("Exit code", std::to_string(ReportExitCode(report)));
        out.Field("Target PID", Number(report.pid));
        out.Field("Target path", report.path ? Utf8(*report.path) : "not supplied");
        out.Field("Duration (ms)", std::to_string(report.duration_ms));
        out.Heading(2, "Execution and evidence");
        out.Field("Command accepted", Bool(report.accepted));
        out.Field("Operation completed", Bool(success));
        out.Field("Outcome verified", Bool(success && report.command != "doctor"));
        out.Field("Interpretation", "Status describes command completion. Outcome verified refers only to the command's stated scope, never DLL loading or initialization.");
        const auto evidence = ReportEvidence(report);
        if (evidence.empty()) out.Field("Evidence", "none");
        for (const auto& item : evidence) {
            out.Field("Source", item.source);
            out.Field("Evidence", item.description);
        }
        if (!success) {
            out.Heading(2, "Errors");
            for (const auto& error : report.errors) WriteError(out, error);
        } else if (const auto* doctor = std::get_if<DoctorReport>(&report.data)) WriteDoctor(out, *doctor);
        else {
            out.Heading(2, "Details");
            const auto details = Render(report, false);
            if (!details) return details.error();
            out.Code(details.value());
        }
        out.Heading(2, "Complete JSON report");
        out.Code(json.value(), true);
        return out.Finish();
    } catch (const Error& error) { return error; }
}
}
