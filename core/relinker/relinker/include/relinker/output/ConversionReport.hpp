#ifndef RELINKER_OUTPUT_CONVERSIONREPORT_HPP
#define RELINKER_OUTPUT_CONVERSIONREPORT_HPP

#include <codegen/CodegenTypes.hpp>
#include <relinker/domain/RelinkResult.hpp>
#include <string>
#include <vector>

namespace Relinker {

struct ConversionReport {
    std::size_t NidsIn = 0;
    std::size_t NidsOut = 0;
    std::size_t InPlaceCount = 0;
    std::size_t StubCount = 0;
    std::vector<Codegen::ResidualSite> Residuals;
    std::size_t UnprovenBytes = 0;
};

class ConversionReportWriter {
public:
    std::string Write(const ConversionReport& report) const;
};

}

#endif
