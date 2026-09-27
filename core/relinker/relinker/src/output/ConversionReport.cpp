#include <relinker/output/ConversionReport.hpp>
#include <sstream>

namespace Relinker {

static std::string _jsonString(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('"');
    for (char c : value) {
        if (c == '"')
            out += "\\\"";
        else if (c == '\\')
            out += "\\\\";
        else if (c == '\n')
            out += "\\n";
        else if (c == '\r')
            out += "\\r";
        else if (c == '\t')
            out += "\\t";
        else
            out.push_back(c);
    }
    out.push_back('"');
    return out;
}

std::string ConversionReportWriter::Write(const ConversionReport& report) const {
    std::ostringstream out;
    out << "{\n";
    out << "  \"nids_in\": " << report.NidsIn << ",\n";
    out << "  \"nids_out\": " << report.NidsOut << ",\n";
    out << "  \"nids_filtered\": " << (report.NidsIn - report.NidsOut) << ",\n";
    out << "  \"in_place\": " << report.InPlaceCount << ",\n";
    out << "  \"stubs\": " << report.StubCount << ",\n";
    out << "  \"unproven_bytes\": " << report.UnprovenBytes << ",\n";
    out << "  \"residual\": [\n";
    for (std::size_t i = 0; i < report.Residuals.size(); ++i) {
        const auto& site = report.Residuals[i];
        out << "    {\"file_offset\": \"0x" << std::hex << std::uppercase << site.FileOffset << std::dec
            << "\", \"address\": \"0x" << std::hex << std::uppercase << site.Address << std::dec
            << "\", \"mnemonic\": " << _jsonString(site.Mnemonic)
            << ", \"length\": " << site.Length << "}";
        if (i + 1 < report.Residuals.size())
            out << ",";
        out << "\n";
    }
    out << "  ]\n";
    out << "}\n";
    return out.str();
}

}
