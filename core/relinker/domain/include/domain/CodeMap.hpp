#ifndef DOMAIN_CODEMAP_HPP
#define DOMAIN_CODEMAP_HPP

#include <domain/Types.hpp>
#include <map>
#include <set>
#include <vector>

namespace Domain {

struct AddressRange {
    VirtualAddress Begin = 0;
    VirtualAddress End = 0;
};

struct CodeMap {
    // Proven instruction starts from recursive descent.
    std::set<VirtualAddress> Starts;
    // FDE/symbol ranges [begin,end).
    std::map<VirtualAddress, VirtualAddress> Functions;
    // Direct branch targets + relocation roots.
    std::set<VirtualAddress> BranchTargets;
    // Executable bytes never reached.
    std::vector<AddressRange> Unproven;

    [[nodiscard]] bool Contains(VirtualAddress address) const {
        return Starts.find(address) != Starts.end();
    }
    [[nodiscard]] bool IsTarget(VirtualAddress address) const {
        return BranchTargets.find(address) != BranchTargets.end();
    }
    // Why upper_bound: a branch to the site start is fine; a branch
    // landing strictly inside (site, site+len) would execute mid-instruction.
    [[nodiscard]] bool BranchEntersSite(VirtualAddress site, std::size_t length) const {
        auto hit = BranchTargets.upper_bound(site);
        return hit != BranchTargets.end() && *hit < site + length;
    }
};

}

#endif
