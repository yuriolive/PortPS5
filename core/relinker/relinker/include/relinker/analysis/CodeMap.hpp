#ifndef RELINKER_ANALYSIS_CODEMAP_HPP
#define RELINKER_ANALYSIS_CODEMAP_HPP

#include <domain/CodeMap.hpp>
#include <domain/Types.hpp>
#include <vector>

namespace Relinker {

Domain::CodeMap BuildCodeMap(
    const std::vector<std::uint8_t>& bytes,
    const std::vector<Domain::ProgramHeader>& headers);

}

#endif
