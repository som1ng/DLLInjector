#include "saved_report.h"
#include "common/win32.h"
#include "report_schemas.h"
#include <jsoncons/json.hpp>
#include <jsoncons/json_filter.hpp>
#include <jsoncons/json_reader.hpp>
#include <jsoncons_ext/jsonschema/jsonschema.hpp>
#include <algorithm>
#include <chrono>
#include <unordered_set>

namespace dllinjector::cli {
namespace {
using Json = jsoncons::json;
[[noreturn]] void Fail(ErrorCategory category, const char* stage, const char* message) {
    throw Error{category, stage, message, {}, {}};
}
class Deadline {
public:
    explicit Deadline(std::uint32_t timeout) : start_(std::chrono::steady_clock::now()), timeout_(timeout) {}
    void Check() const {
        if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_).count() >= timeout_)
            Fail(ErrorCategory::Timeout, "report.deadline", "Saved report read/validation deadline expired.");
    }
private:
    std::chrono::steady_clock::time_point start_;
    std::uint32_t timeout_;
};

// Reject duplicates before the DOM can silently discard them; bound parser work.
class InputFilter : public jsoncons::json_filter {
public:
    InputFilter(jsoncons::json_visitor& target, const Deadline& deadline) : jsoncons::json_filter(target), deadline_(deadline) {}
private:
    void Count() {
        deadline_.Check();
        if (++events_ > 250000) Fail(ErrorCategory::Unsupported, "report.limits", "Report exceeds 250000 JSON containers, keys and scalar values.");
    }
    bool visit_begin_object(jsoncons::semantic_tag tag, const jsoncons::ser_context& context, std::error_code& ec) override {
        Count(); keys_.emplace_back(); return destination().begin_object(tag, context, ec);
    }
    bool visit_end_object(const jsoncons::ser_context& context, std::error_code& ec) override {
        keys_.pop_back(); return destination().end_object(context, ec);
    }
    bool visit_begin_array(jsoncons::semantic_tag tag, const jsoncons::ser_context& context, std::error_code& ec) override {
        Count(); return destination().begin_array(tag, context, ec);
    }
    bool visit_key(const jsoncons::string_view& value, const jsoncons::ser_context& context, std::error_code& ec) override {
        Count();
        if (value.find('\0') != jsoncons::string_view::npos)
            Fail(ErrorCategory::InvalidArgument, "report.parse", "Embedded NUL in JSON keys is not supported.");
        if (!keys_.back().emplace(value.data(), value.size()).second)
            Fail(ErrorCategory::InvalidArgument, "report.parse", "Duplicate JSON object keys are not accepted.");
        return destination().key(value, context, ec);
    }
    bool visit_string(const jsoncons::string_view& value, jsoncons::semantic_tag tag, const jsoncons::ser_context& context, std::error_code& ec) override {
        Count();
        if (tag == jsoncons::semantic_tag::bigint)
            Fail(ErrorCategory::Unsupported, "report.limits", "JSON integers must fit int64 or uint64.");
        if (tag == jsoncons::semantic_tag::bigdec)
            Fail(ErrorCategory::InvalidArgument, "report.parse", "Use integer literals; floating-point and exponent JSON numbers are not accepted.");
        // Schema regex matching uses C strings; reject NUL instead of validating a prefix.
        if (value.find('\0') != jsoncons::string_view::npos)
            Fail(ErrorCategory::InvalidArgument, "report.parse", "Embedded NUL in saved report strings is not supported.");
        return destination().string_value(value, tag, context, ec);
    }
    bool visit_uint64(std::uint64_t value, jsoncons::semantic_tag tag, const jsoncons::ser_context& context, std::error_code& ec) override {
        Count(); return destination().uint64_value(value, tag, context, ec);
    }
    bool visit_int64(std::int64_t value, jsoncons::semantic_tag tag, const jsoncons::ser_context& context, std::error_code& ec) override {
        Count(); return destination().int64_value(value, tag, context, ec);
    }
    bool visit_double(double, jsoncons::semantic_tag, const jsoncons::ser_context&, std::error_code&) override {
        Fail(ErrorCategory::InvalidArgument, "report.parse", "Use integer literals; floating-point and exponent JSON numbers are not accepted.");
    }
    bool visit_bool(bool value, jsoncons::semantic_tag tag, const jsoncons::ser_context& context, std::error_code& ec) override {
        Count(); return destination().bool_value(value, tag, context, ec);
    }
    bool visit_null(jsoncons::semantic_tag tag, const jsoncons::ser_context& context, std::error_code& ec) override {
        Count(); return destination().null_value(tag, context, ec);
    }
    const Deadline& deadline_;
    std::size_t events_ = 0;
    std::vector<std::unordered_set<std::string>> keys_;
};

std::string String(const Json& value) { return value.as<std::string>(); }
std::string Dump(const Json& value, bool pretty = false) {
    std::string text;
    if (pretty) value.dump_pretty(text);
    else value.dump(text);
    return text;
}
std::vector<FindingEvidence> Evidence(const Json& array) {
    std::vector<FindingEvidence> result;
    for (const auto& value : array.array_range())
        result.push_back({String(value.at("source")), String(value.at("description"))});
    return result;
}
Error ReadError(const Json& value) {
    const auto category = String(value.at("category"));
    ErrorCategory mapped = ErrorCategory::System;
    for (const auto kind : {ErrorCategory::InvalidArgument, ErrorCategory::AccessDenied, ErrorCategory::NotFound,
        ErrorCategory::Unsupported, ErrorCategory::System, ErrorCategory::Timeout, ErrorCategory::Io, ErrorCategory::MalformedPe})
        if (category == CategoryName(kind)) mapped = kind;
    Error error{mapped, String(value.at("stage")), String(value.at("message")), {}, {}, value.at("verified").as<bool>()};
    if (!value.at("win32").is_null()) error.win32 = value.at("win32").as<std::uint32_t>();
    if (!value.at("ntstatus").is_null()) error.ntstatus = value.at("ntstatus").as<std::int32_t>();
    return error;
}
SavedReport Parse(const std::string& bytes, const Deadline& deadline) {
    deadline.Check();
    if (bytes.size() > MaxSavedReportBytes) Fail(ErrorCategory::Unsupported, "report.limits", "Saved reports are limited to 16 MiB.");
    jsoncons::json_decoder<Json> decoder;
    InputFilter filter(decoder, deadline);
    jsoncons::json_options options;
    options.max_nesting_depth(64).err_handler([](jsoncons::json_errc, const jsoncons::ser_context&) { return false; });
    jsoncons::json_string_reader reader(bytes, filter, options);
    reader.read();
    deadline.Check();
    const auto document = decoder.get_result();
    if (!document.is_object() || !document.contains("schema_version") || !document.at("schema_version").is_string())
        Fail(ErrorCategory::InvalidArgument, "report.schema", "Expected a versioned DLLInjector JSON report object.");
    const auto version = String(document.at("schema_version"));
    constexpr std::string_view schemas[] = {ReportSchema0, ReportSchema1, ReportSchema2, ReportSchema3, ReportSchema4, ReportSchema5, ReportSchema6};
    if (version.size() != 3 || version[0] != '1' || version[1] != '.' || version[2] < '0' || version[2] > '6')
        Fail(ErrorCategory::Unsupported, "report.version", "Supported saved report schema versions are 1.0 through 1.6.");
    // Bound the two unbounded repetition patterns before invoking std::regex.
    if (document.contains("tool_version") && document.at("tool_version").is_string() && document.at("tool_version").as_string_view().size() > 128)
        Fail(ErrorCategory::Unsupported, "report.limits", "Tool version strings are limited to 128 UTF-8 bytes.");
    if (document.contains("findings") && document.at("findings").is_array()) {
        for (const auto& finding : document.at("findings").array_range()) {
            if (!finding.is_object() || !finding.contains("documentation") || !finding.at("documentation").is_array()) continue;
            for (const auto& url : finding.at("documentation").array_range())
                if (url.is_string() && url.as_string_view().size() > 2048)
                    Fail(ErrorCategory::Unsupported, "report.limits", "Documentation URLs are limited to 2048 UTF-8 bytes.");
        }
    }
    const auto schemaText = schemas[static_cast<std::size_t>(version[2] - '0')];
    // Only embedded schemas are compiled; no URI resolver or user schema is installed.
    const auto compiled = jsoncons::jsonschema::make_json_schema(Json::parse(schemaText));
    deadline.Check();
    const bool valid = compiled.is_valid(document);
    deadline.Check();
    if (!valid) Fail(ErrorCategory::InvalidArgument, "report.schema", "The report does not match its declared bundled JSON Schema.");
    SavedReport result;
    result.schema_version = version;
    result.tool_version = String(document.at("tool_version"));
    result.command = String(document.at("command"));
    result.status = String(document.at("status"));
    result.target_pid = Dump(document.at("target").at("pid"));
    const auto& target = document.at("target");
    result.target_path = target.contains("path") && !target.at("path").is_null() ? String(target.at("path")) : "not supplied";
    result.duration_ms = Dump(document.at("duration_ms"));
    const auto& execution = document.at("execution");
    result.accepted = execution.at("command_accepted").as<bool>();
    result.completed = execution.at("operation_completed").as<bool>();
    result.verified = execution.at("outcome_verified").as<bool>();
    result.evidence = Evidence(document.at("evidence"));
    for (const auto& error : document.at("errors").array_range()) result.errors.push_back(ReadError(error));
    for (const auto& f : document.at("findings").array_range()) {
        const auto severity = String(f.at("severity"));
        DoctorFinding finding{String(f.at("rule_id")), severity == "error" ? FindingSeverity::Error :
            severity == "warning" ? FindingSeverity::Warning : FindingSeverity::Info,
            String(f.at("message")), Evidence(f.at("evidence")), String(f.at("confidence")), String(f.at("recommended_next_step")), {}};
        for (const auto& url : f.at("documentation").array_range()) finding.documentation.push_back(String(url));
        result.findings.push_back(std::move(finding));
    }
    result.data_json = Dump(document.at("data"), true);
    // JSON string literals cannot contain a raw CR; only document whitespace changes.
    const std::size_t start = bytes.compare(0, 3, "\xef\xbb\xbf") == 0 ? 3 : 0;
    for (std::size_t i = start; i < bytes.size(); ++i) {
        if (bytes[i] == '\r') {
            if (i + 1 < bytes.size() && bytes[i + 1] == '\n') ++i;
            result.original_json += '\n';
        } else result.original_json += bytes[i];
    }
    deadline.Check();
    return result;
}
Error ParseError(const jsoncons::ser_error& error) {
    if (error.code() == jsoncons::json_errc::max_nesting_depth_exceeded)
        return {ErrorCategory::Unsupported, "report.limits", "Saved report nesting exceeds 64 levels.", {}, {}};
    return {ErrorCategory::InvalidArgument, "report.parse", "Input must be one strict UTF-8 JSON document without duplicate keys.", {}, {}};
}
Error FileError(DWORD code, const char* stage) {
    const auto category = code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND ? ErrorCategory::NotFound
        : code == ERROR_ACCESS_DENIED ? ErrorCategory::AccessDenied : ErrorCategory::Io;
    return {category, stage, "Could not read the saved report; check its path, permissions and sharing mode.", code, {}};
}
Error MemoryError() { return {ErrorCategory::System, "report.memory", "Insufficient memory for saved report conversion.", {}, {}}; }
}

Result<SavedReport> ParseSavedReport(const std::string& bytes, std::uint32_t timeout_ms) {
    try { return Parse(bytes, Deadline(timeout_ms)); }
    catch (const Error& error) { return error; }
    catch (const jsoncons::ser_error& error) { return ParseError(error); }
    catch (const std::bad_alloc&) { return MemoryError(); }
}
Result<SavedReport> ReadSavedReport(const std::wstring& path, std::uint32_t timeout_ms) {
    try {
        const Deadline deadline(timeout_ms);
        deadline.Check();
        if (path.empty() || path.find(L'\0') != std::wstring::npos)
            Fail(ErrorCategory::InvalidArgument, "report.path", "A nonempty file path without embedded NUL is required.");
        const DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
        if (!needed) return FileError(GetLastError(), "report.path");
        std::wstring full(needed, L'\0');
        const DWORD length = GetFullPathNameW(path.c_str(), needed, full.data(), nullptr);
        if (!length) return FileError(GetLastError(), "report.path");
        if (length >= needed) Fail(ErrorCategory::Io, "report.path", "Input path changed while resolving it; use an absolute path.");
        full.resize(length);
        if (full.size() < 3 || full[1] != L':' || full[2] != L'\\' || GetDriveTypeW(full.substr(0, 3).c_str()) == DRIVE_REMOTE)
            Fail(ErrorCategory::Unsupported, "report.path", "Use a local drive file; UNC, device and remote drive paths are unsupported.");
        deadline.Check();
        detail::UniqueHandle file(CreateFileW(full.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
        if (!file) return FileError(GetLastError(), "report.open");
        deadline.Check();
        if (GetFileType(file.get()) != FILE_TYPE_DISK)
            Fail(ErrorCategory::Unsupported, "report.file_type", "Saved reports must be regular disk files.");
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(file.get(), &info)) return FileError(GetLastError(), "report.stat");
        if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            Fail(ErrorCategory::InvalidArgument, "report.file_type", "Saved reports must be files, not directories.");
        const std::uint64_t size = (std::uint64_t{info.nFileSizeHigh} << 32) | info.nFileSizeLow;
        if (size > MaxSavedReportBytes) Fail(ErrorCategory::Unsupported, "report.limits", "Saved reports are limited to 16 MiB.");
        std::string bytes(static_cast<std::size_t>(size), '\0');
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            deadline.Check();
            const auto wanted = static_cast<DWORD>((std::min)(bytes.size() - offset, std::size_t{1024 * 1024}));
            DWORD count = 0;
            if (!ReadFile(file.get(), bytes.data() + offset, wanted, &count, nullptr)) return FileError(GetLastError(), "report.read");
            if (!count) Fail(ErrorCategory::Io, "report.read", "Saved report ended before its reported size.");
            offset += count;
        }
        return Parse(bytes, deadline);
    } catch (const Error& error) { return error; }
    catch (const jsoncons::ser_error& error) { return ParseError(error); }
    catch (const std::bad_alloc&) { return MemoryError(); }
}
}
