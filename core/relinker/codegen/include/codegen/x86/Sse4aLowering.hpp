#ifndef CODEGEN_X86_SSE4ALOWERING_HPP
#define CODEGEN_X86_SSE4ALOWERING_HPP

#include <codegen/x86/Sse4aOperands.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace Codegen {

struct LoweredBody {
    std::vector<std::uint8_t> Bytes;
    std::size_t ReturnBranchOffset;
};

class Sse4aLowering {
public:
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> LowerInPlace(const Sse4aOperands& operands, std::size_t originalLength) const;
    [[nodiscard]] LoweredBody LowerOutOfLine(const Sse4aOperands& operands) const;
};

}

#endif
