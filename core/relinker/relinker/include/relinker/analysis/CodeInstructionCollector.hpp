#ifndef RELINKER_CODEINSTRUCTIONCOLLECTOR_HPP
#define RELINKER_CODEINSTRUCTIONCOLLECTOR_HPP

#include <domain/Types.hpp>
#include <map>
#include <set>
#include <vector>

namespace Relinker {

struct CollectedCode {
    std::set<Domain::VirtualAddress> Starts;
    std::map<Domain::VirtualAddress, Domain::VirtualAddress> Functions;
};

class CodeInstructionCollector {
public:
    std::set<Domain::VirtualAddress> Collect(const std::vector<std::uint8_t>& bytes, const std::vector<Domain::ProgramHeader>& headers) const;
    CollectedCode CollectDetailed(const std::vector<std::uint8_t>& bytes, const std::vector<Domain::ProgramHeader>& headers) const;
};

}

#endif
