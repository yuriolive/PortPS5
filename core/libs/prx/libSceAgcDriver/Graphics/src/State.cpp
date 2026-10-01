// Decodes the PM4 context/shader register file into a State (AGC graphics subsystem).
// Translates blend, raster, viewport, colour target and depth/stencil registers to Vulkan
// values and rejects unsupported combinations with logged errors (never silently drawn).
// Pure functions over QueueState; no guest memory is touched except CheckGpuRange validation.
#include "prx/libSceAgcDriver/Graphics/include/State.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libc/include/General.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <sstream>

namespace AgcDriver::Graphics {
namespace {

std::uint32_t read(const Registers& registers, std::uint32_t offset, const char* bank = "context") {
    const auto it = registers.find(offset);
    if (it == registers.end()) {
        std::ostringstream message;
        message << "missing register in " << bank << " bank at DWORD 0x" << std::hex << offset << " (" << std::dec << offset << ')';
        throw std::runtime_error("AGC graphics: " + message.str());
    }
    return it->second;
}

float readFloat(const Registers& registers, std::uint32_t offset) {
    const auto value = std::bit_cast<float>(read(registers, offset));
    Require(std::isfinite(value), "non-finite register at DWORD " + std::to_string(offset));
    return value;
}

void zero(const Registers& registers, std::uint32_t offset, std::uint32_t mask, const char* name, const char* bank = "context") {
    Require((read(registers, offset, bank) & mask) == 0, std::string(name) + " is unsupported");
}

VkBlendFactor blendFactor(std::uint32_t value) {
    switch (value) {
        case 0: return VK_BLEND_FACTOR_ZERO;
        case 1: return VK_BLEND_FACTOR_ONE;
        case 2: return VK_BLEND_FACTOR_SRC_COLOR;
        case 3: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case 4: return VK_BLEND_FACTOR_SRC_ALPHA;
        case 5: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case 6: return VK_BLEND_FACTOR_DST_ALPHA;
        case 7: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case 8: return VK_BLEND_FACTOR_DST_COLOR;
        case 9: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case 10: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
        case 13: return VK_BLEND_FACTOR_CONSTANT_COLOR;
        case 14: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
        case 19: return VK_BLEND_FACTOR_CONSTANT_ALPHA;
        case 20: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
        default: throw std::runtime_error("AGC graphics: unsupported blend factor " + std::to_string(value));
    }
}

VkBlendOp blendOp(std::uint32_t value) {
    switch (value) {
        case 0: return VK_BLEND_OP_ADD;
        case 1: return VK_BLEND_OP_SUBTRACT;
        case 2: return VK_BLEND_OP_MIN;
        case 3: return VK_BLEND_OP_MAX;
        case 4: return VK_BLEND_OP_REVERSE_SUBTRACT;
        default: throw std::runtime_error("AGC graphics: unsupported blend operation " + std::to_string(value));
    }
}

std::uint32_t readOr(const Registers& registers, std::uint32_t offset, std::uint32_t fallback) {
    const auto it = registers.find(offset);
    return it == registers.end() ? fallback : it->second;
}

/**
 * Decodes one stencil face. Shared by front and back: opShift selects the
 * three 4-bit ops in DB_STENCIL_CONTROL and refMask is the face's
 * DB_STENCILREFMASK word (test value 0-7, mask 8-15, write mask 16-23,
 * op value 24-31). Enum values per AMD's gfx10 register database (StencilOp,
 * DB_STENCIL_CONTROL).
 *
 * Vulkan has one reference per face that serves as both the compare reference
 * and the REPLACE value, while the hardware has three sources: REPLACE_TEST (3)
 * writes the test value, REPLACE_OP (4) the op value and ONES (2) 0xff. Every
 * replace-type op that can write (write mask nonzero) therefore has to agree on
 * one value under the write mask. When that value differs from the test value
 * the reference is repurposed as the replace value, which is only sound when the
 * stencil function ignores the reference (NEVER or ALWAYS); otherwise the face
 * is rejected instead of being mistranslated.
 */
VkStencilOpState decodeStencilFace(std::uint32_t control, std::uint32_t opShift, std::uint32_t function, std::uint32_t refMask) {
    VkStencilOpState face{};
    const std::uint32_t ops[3] = {(control >> opShift) & 0xfu, (control >> (opShift + 4u)) & 0xfu, (control >> (opShift + 8u)) & 0xfu};
    const auto writeMask = (refMask >> 16u) & 0xffu;
    const auto opValue = (refMask >> 24u) & 0xffu;
    const auto reference = refMask & 0xffu;
    std::uint32_t replaceValue = reference;
    bool replaces = false;
    for (const auto op : ops) {
        if (writeMask == 0 || (op != 2 && op != 3 && op != 4)) continue;
        const auto value = (op == 3 ? reference : op == 4 ? opValue : 0xffu) & writeMask;
        Require(!replaces || value == replaceValue, "stencil replace operations that write different values in one face are unsupported");
        replaces = true;
        replaceValue = value;
    }
    const bool referenceIgnored = function == 0 || function == 7;
    std::uint32_t vulkanReference = reference;
    if (replaces && replaceValue != (reference & writeMask)) {
        Require(referenceIgnored, "stencil REPLACE_OP/ONES with a value different from the test value is unsupported when the stencil function reads the reference");
        vulkanReference = replaceValue;
    }
    const auto translate = [&](std::uint32_t op) { return writeMask != 0 && op == 2 ? VK_STENCIL_OP_REPLACE : DecodeStencilOp(op, writeMask, opValue); };
    face.failOp = translate(ops[0]);
    face.passOp = translate(ops[1]);
    face.depthFailOp = translate(ops[2]);
    face.compareOp = DecodeCompareOp(function);
    face.compareMask = (refMask >> 8u) & 0xffu;
    face.writeMask = writeMask;
    face.reference = vulkanReference;
    return face;
}

/**
 * Decodes the bound depth/stencil surface. DB_Z_INFO.FORMAT (bits 0-1: 0 none, 1 Z16,
 * 3 Z32_FLOAT; 2 is reserved) and DB_STENCIL_INFO.FORMAT (bit 0) decide whether a
 * surface is bound at all; with neither set the surface registers are not read, which
 * keeps the no-depth-buffer case (2D layering) inert exactly as on hardware.
 *
 * Bound surfaces must be single-sample, single-slice, single-mip (the host image is one
 * 2D layer). TILE_SURFACE_ENABLE, expclear, SW_MODE and the other layout/HTILE bits are
 * metadata of the guest's memory layout and are ignored: the image is host-owned and
 * never retiled. Read and write bases must agree (the host image has one identity).
 * DB_RENDER_CONTROL copy-to-colour bits are rejected; compression/resummarize bits are
 * metadata and inert.
 *
 * depthReadOnly/stencilReadOnly report DB_DEPTH_VIEW Z_READ_ONLY/STENCIL_READ_ONLY
 * (bits 24/25) so the caller can drop writes.
 */
DepthTarget DecodeDepthTarget(const Registers& cx, bool& depthReadOnly, bool& stencilReadOnly) {
    DepthTarget target;
    depthReadOnly = stencilReadOnly = false;
    const auto zInfo = readOr(cx, 0x10, 0);
    const auto stencilInfo = readOr(cx, 0x11, 0);
    const auto format = zInfo & 3u;
    Require(format != 2, "DB_Z_INFO.FORMAT 2 is reserved");
    target.depthFormat = static_cast<DepthSurfaceFormat>(format);
    target.hasStencil = (stencilInfo & 1u) != 0;
    if (!target.Bound()) return target;
    Require(((zInfo >> 2u) & 3u) == 0, "multisampled depth surfaces are unsupported");
    Require((zInfo & 0x1000u) == 0 && (stencilInfo & 0x1000u) == 0, "partially resident depth surfaces are unsupported");
    Require(((zInfo >> 16u) & 0xfu) == 0, "mipmapped depth surfaces are unsupported");
    const auto view = readOr(cx, 0x2, 0);
    const auto sliceStart = (view & 0x7ffu) | (((view >> 11u) & 3u) << 11u);
    const auto sliceMax = ((view >> 13u) & 0x7ffu) | (((view >> 30u) & 3u) << 11u);
    Require(sliceStart == 0 && sliceMax == 0 && ((view >> 26u) & 0xfu) == 0, "depth array slices and mip views are unsupported");
    depthReadOnly = (view & 0x01000000u) != 0;
    stencilReadOnly = (view & 0x02000000u) != 0;
    const auto size = cx.find(0x7);
    Require(size != cx.end(), "depth surface bound without DB_DEPTH_SIZE_XY");
    Require((size->second & 0xc000c000u) == 0, "reserved DB_DEPTH_SIZE_XY bits");
    target.extent = {(size->second & 0x3fffu) + 1u, ((size->second >> 16u) & 0x3fffu) + 1u};
    const auto address = [&](std::uint32_t low, std::uint32_t high, const char* name) {
        const auto hi = readOr(cx, high, 0);
        Require((hi & ~0xffu) == 0, std::string("invalid ") + name + " address extension");
        return (static_cast<std::uint64_t>(hi) << 40u) | (static_cast<std::uint64_t>(readOr(cx, low, 0)) << 8u);
    };
    if (target.HasDepth()) {
        target.depthAddress = address(0x14, 0x1c, "DB_Z_WRITE_BASE");
        Require(target.depthAddress != 0, "depth surface bound with a null DB_Z_WRITE_BASE");
        Require(address(0x12, 0x1a, "DB_Z_READ_BASE") == target.depthAddress, "separate depth read and write bases are unsupported");
    }
    if (target.hasStencil) {
        target.stencilAddress = address(0x15, 0x1d, "DB_STENCIL_WRITE_BASE");
        Require(address(0x13, 0x1b, "DB_STENCIL_READ_BASE") == target.stencilAddress, "separate stencil read and write bases are unsupported");
        if (target.stencilAddress == 0) {
            // A valid DB_STENCIL_INFO.FORMAT with a null base means the title never allocated the stencil plane
            // (observed: Z32F + STENCIL_8 declared, both bases 0, DB_DEPTH_CONTROL.STENCIL_ENABLE = 0). Hardware never
            // touches the plane unless stencil is tested/written (DB_DEPTH_CONTROL bit 0) or cleared
            // (DB_RENDER_CONTROL bit 1), so an unused plane is simply absent. Using it with no memory is a guest
            // error and stays fatal (docs/spec/gpu-driver.md, no silent skips).
            const bool stencilUsed = (readOr(cx, 0x200, 0) & 1u) != 0 || (readOr(cx, 0x0, 0) & 2u) != 0;
            Require(!stencilUsed, "stencil enabled or cleared with a null DB_STENCIL_WRITE_BASE");
            target.hasStencil = false;
        }
    }
    const auto control = readOr(cx, 0x0, 0);
    // Accepted: DEPTH/STENCIL_CLEAR_ENABLE (0, 1), RESUMMARIZE and the two COMPRESS_DISABLE
    // bits (4-6), the copy-sample fields (7-11, inert without a copy) and DECOMPRESS_ENABLE
    // (12). DEPTH_COPY / STENCIL_COPY (2, 3) move depth into a colour target: unsupported.
    Require((control & ~0x1ff3u) == 0, "depth/stencil copy to colour or reserved DB_RENDER_CONTROL bits are unsupported");
    target.clearDepth = (control & 1u) != 0 && target.HasDepth();
    target.clearStencil = (control & 2u) != 0 && target.hasStencil;
    if (target.clearDepth) {
        target.clearDepthValue = readFloat(cx, 0xb);
        Require(target.clearDepthValue >= 0 && target.clearDepthValue <= 1, "depth clear value outside [0, 1]");
    }
    if (target.clearStencil) target.clearStencilValue = static_cast<std::uint8_t>(readOr(cx, 0xa, 0) & 0xffu);
    return target;
}

/**
 * Decodes DB_DEPTH_CONTROL and its companions. Reset state is all zero, so the
 * stencil and reference registers fall back to 0 when the title never wrote
 * them. DB_DEPTH_CONTROL bits 11..19 and 23..29 are reserved and rejected.
 * With no bound surface the hardware ignores the tests, which matches Vulkan
 * ignoring the state for a render pass without a depth attachment; with one
 * the same fields drive the host depth image (ToVulkan reconciles them with
 * the DepthTarget). Z_READ_ONLY / STENCIL_READ_ONLY from DB_DEPTH_VIEW drop
 * depth writes and turn every stencil op into a keep. Stencil ops are only
 * decoded when the stencil test is enabled: a disabled test passes every pixel,
 * so stale ops left in the registers must not reject a draw.
 * The conditional colour-write bits (30/31) are decoded here and applied by
 * DecodeState, which owns the colour write mask.
 */
DepthStencilState DecodeDepthStencil(const Registers& cx, bool depthReadOnly, bool stencilReadOnly) {
    const auto control = read(cx, 0x200);
    Require((control & 0x3f8ff800u) == 0, "reserved DB_DEPTH_CONTROL bits");
    DepthStencilState result;
    result.stencilTestEnable = (control & 1u) != 0;
    result.depthTestEnable = (control & 2u) != 0;
    // Z_WRITE_ENABLE is inert without Z_ENABLE, so it is cleared here (upstream 52ffef82).
    result.depthWriteEnable = result.depthTestEnable && (control & 4u) != 0 && !depthReadOnly;
    result.depthBoundsTestEnable = (control & 8u) != 0;
    result.depthCompareOp = DecodeCompareOp((control >> 4u) & 7u);
    result.colorWriteOnDepthFail = (control & 0x40000000u) != 0;
    result.disableColorWriteOnDepthPass = (control & 0x80000000u) != 0;
    const bool backface = (control & 0x80u) != 0;
    const auto stencilControl = readOr(cx, 0x10b, 0);
    result.front.compareOp = result.back.compareOp = VK_COMPARE_OP_ALWAYS;
    if (result.stencilTestEnable) {
        result.front = decodeStencilFace(stencilControl, 0, (control >> 8u) & 7u, readOr(cx, 0x10c, 0));
        result.back = backface ? decodeStencilFace(stencilControl, 12, (control >> 20u) & 7u, readOr(cx, 0x10d, 0)) : result.front;
        if (stencilReadOnly) {
            for (auto* face : {&result.front, &result.back}) {
                face->failOp = face->passOp = face->depthFailOp = VK_STENCIL_OP_KEEP;
                face->writeMask = 0;
            }
        }
    }
    if (result.depthBoundsTestEnable) {
        result.minDepthBounds = readFloat(cx, 0x8);
        result.maxDepthBounds = readFloat(cx, 0x9);
        Require(result.minDepthBounds <= result.maxDepthBounds, "inverted depth bounds");
    }
    return result;
}

}  // namespace

/**
 * Applies DB_DEPTH_CONTROL bits 30 (ENABLE_COLOR_WRITES_ON_DEPTH_FAIL) and 31
 * (DISABLE_COLOR_WRITES_ON_DEPTH_PASS). Colour is normally written where the depth test
 * passes. Bit 31 alone suppresses colour everywhere, so the colour write mask becomes 0
 * while the depth/stencil tests still run (Vulkan keeps testing with a zero write mask).
 * With both bits set colour is written only where the depth test fails: that is also "never"
 * when the test cannot fail (no depth test or no depth aspect), so the mask is 0 too.
 * A depth test that can fail combined with bit 30 would need colour written on both outcomes
 * or only on failure; that takes two passes with an inverted compare and is rejected rather
 * than mistranslated (see docs/spec/gpu-driver.md "Conditional colour writes").
 */
void ApplyConditionalColorWrites(State& state) {
    const auto& ds = state.depthStencil;
    if (!state.hasColorTarget) return;
    const bool depthCanFail = ds.depthTestEnable && state.depthTarget.HasDepth();
    Require(!(ds.colorWriteOnDepthFail && depthCanFail), "DB_DEPTH_CONTROL bit 30 (colour writes on depth fail) with an active depth test is unsupported");
    if (ds.disableColorWriteOnDepthPass) state.blend.colorWriteMask = 0;
}

/**
 * A DB_RENDER_CONTROL clear draw writes the clear value over the primitives it covers. The
 * host maps it to a render-pass loadOp CLEAR, which clears the whole attachment, so the draw
 * must be a rect-list (the AMD clear convention) whose scissor covers the entire depth
 * surface; anything smaller would clear more than the guest asked for and is rejected.
 */
void ValidateDepthClear(const State& state) {
    const auto& target = state.depthTarget;
    if (!target.clearDepth && !target.clearStencil) return;
    Require(state.rectList, "depth/stencil clear requires a rect-list draw");
    Require(state.scissor.offset.x == 0 && state.scissor.offset.y == 0 && state.scissor.extent.width == target.extent.width && state.scissor.extent.height == target.extent.height && state.renderExtent.width == target.extent.width && state.renderExtent.height == target.extent.height,
            "partial depth/stencil clears (scissor smaller than the depth surface) are unsupported");
}

namespace {

void intersect(VkRect2D& result, const Registers& registers, std::uint32_t offset, bool screen) {
    const auto tl = read(registers, offset);
    const auto br = read(registers, offset + 1);
    if (!screen) Require((tl & 0x80008000u) == 0x80000000u && (br & 0x80008000u) == 0, "scissor window offsets or reserved bits are unsupported");
    const auto x = tl & 0xffffu;
    const auto y = (tl >> 16u) & (screen ? 0xffffu : 0x7fffu);
    const auto right = br & 0xffffu;
    const auto bottom = br >> 16u;
    Require(x <= right && y <= bottom, "inverted scissor rectangle");
    const auto oldRight = static_cast<std::uint32_t>(result.offset.x) + result.extent.width;
    const auto oldBottom = static_cast<std::uint32_t>(result.offset.y) + result.extent.height;
    const auto left = std::max(static_cast<std::uint32_t>(result.offset.x), x);
    const auto top = std::max(static_cast<std::uint32_t>(result.offset.y), y);
    result.offset = {static_cast<std::int32_t>(left), static_cast<std::int32_t>(top)};
    result.extent = {std::min(oldRight, right) > left ? std::min(oldRight, right) - left : 0, std::min(oldBottom, bottom) > top ? std::min(oldBottom, bottom) - top : 0};
}

}

ShaderStages DecodeShaderStages(const QueueState& queue) {
    const auto value = read(queue.context, 0x2d5);
    std::ostringstream prefix;
    prefix << "VGT_SHADER_STAGES_EN=0x" << std::hex << value << ": ";
    const auto validate = [&](bool condition, const char* reason) { Require(condition, prefix.str() + reason); };
    validate((value & 0xfc000000u) == 0, "reserved stage bits are set");
    validate((value & 3u) != 3u && ((value >> 3u) & 3u) != 3u && ((value >> 6u) & 3u) != 3u, "reserved LS_EN, ES_EN or VS_EN encoding");
    const auto primitive = read(queue.userConfig, 0x242, "user-config");
    const bool tessellation = primitive == 9;
    const bool geometry = (value & 0x20u) != 0;
    validate(tessellation == ((value & 4u) != 0), "Patch topology and HS_EN disagree");
    validate(!tessellation || !geometry, "combined tessellation and geometry is unsupported by the reference path");
    const auto path = tessellation ? ShaderPath::Tessellation : geometry ? ShaderPath::Geometry : ShaderPath::Vertex;
    ShaderStages result{path, value, (value & 0x00400000u) != 0 ? 32u : 64u, (read(queue.context, 0x1b6) & 0x8000u) != 0 ? 32u : 64u, {}, {}};
    if (path == ShaderPath::Vertex) {
        validate((value & 0x2000u) != 0, "legacy vertex routing without PRIMGEN_EN is unsupported");
        validate((value & ~0x02402010u) == 0, "unsupported vertex routing, scheduling or wave-ID state");
    } else if (path == ShaderPath::Tessellation) {
        validate((value & 0x00600020u) == 0, "wave32 tessellation or geometry amplification is unsupported");
        validate((value & ~0x0007ed0du) == 0 && (value & 3u) == 1u && ((value >> 3u) & 3u) == 1u, "unsupported tessellation routing");
        const auto config = read(queue.context, 0x2d6);
        const auto parameters = read(queue.context, 0x2db);
        ShaderRecompiler::TessellationConfiguration tess{(config >> 8u) & 0x3fu, (config >> 14u) & 0x3fu, parameters & 3u, (parameters >> 2u) & 3u, (parameters >> 5u) & 3u};
        validate(tess.inputControlPoints != 0 && tess.inputControlPoints <= 32 && tess.outputControlPoints != 0 && tess.outputControlPoints <= 32, "invalid tessellation control-point counts");
        validate(tess.domain == 1 && tess.partitioning == 2 && tess.outputTopology == 2, "only triangular, fractional-odd, clockwise tessellation is supported by the reference path");
        result.tessellation = tess;
    } else {
        validate((value & ~0x0047ec30u) == 0, "unsupported geometry routing, fast launch or wave-ID state");
        const auto group = read(queue.userConfig, 0x25b, "user-config");
        const auto vertices = (group >> 9u) & 0x1ffu;
        const auto primitives = group & 0x1ffu;
        const auto maxVertices = read(queue.context, 0x1ff);
        const auto verticesPerPrimitive = read(queue.context, 0x2ce);
        validate((primitive == 1 || primitive == 2 || primitive == 4 || primitive == 6) && read(queue.context, 0x29b) == 2 && verticesPerPrimitive >= 3, "unsupported geometry input or output assembly");
        const auto inputSize = primitive == 1 ? 1u : primitive == 2 ? 2u : 3u;
        validate(vertices >= inputSize && maxVertices != 0 && maxVertices <= 256 && verticesPerPrimitive <= 256, "invalid geometry subgroup output");
        const auto inputStep = primitive == 6 ? 1u : inputSize;
        const auto groupPrimitives = std::min({primitives, (vertices - inputSize) / inputStep + 1u, maxVertices / verticesPerPrimitive});
        validate(groupPrimitives != 0, "geometry subgroup contains no primitives");
        const auto resources = read(queue.shader, 0x8b, "shader");
        validate(((read(queue.shader, 0x8a, "shader") >> 29u) & 3u) == 3 && ((resources >> 16u) & 3u) == 3, "unsupported geometry VGPR allocation");
        result.mesh = ShaderRecompiler::MeshConfiguration{primitive, groupPrimitives, (groupPrimitives - 1u) * inputStep + inputSize, maxVertices, primitives * (verticesPerPrimitive - 2u), ((maxVertices + result.vertexWaveSize - 1u) / result.vertexWaveSize) * result.vertexWaveSize, ((resources >> 19u) & 0xffu) * 128u, 0};
    }
    return result;
}

State DecodeState(const QueueState& queue) {
    const auto& cx = queue.context;
    State result{};
    result.stages = DecodeShaderStages(queue);
    const auto primitive = read(queue.userConfig, 0x242, "user-config");
    switch (primitive) {
        case 1: Require(result.stages.mesh.has_value(), "point-list vertex rendering requires point-size output support"); result.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST; break;
        case 2: result.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST; break;
        case 7:
        case 17:
            Require(result.stages.path == ShaderPath::Vertex, "rect-list requires vertex routing");
            result.rectList = true;
            result.topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
            break;
        case 9: result.topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST; break;
        case 4: result.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; break;
        case 5: result.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN; break;
        case 6: result.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP; break;
        default: throw std::runtime_error("AGC graphics: unsupported primitive type " + std::to_string(primitive));
    }
    zero(queue.userConfig, 0x24b, ~0u, "primitive restart (GE_MULTI_PRIM_IB_RESET_EN)", "user-config");
    zero(cx, 0x207, ~0u, "clip distances, layer, viewport or auxiliary vertex exports");
    bool depthReadOnly = false;
    bool stencilReadOnly = false;
    result.depthTarget = DecodeDepthTarget(cx, depthReadOnly, stencilReadOnly);
    result.depthStencil = DecodeDepthStencil(cx, depthReadOnly, stencilReadOnly);
    zero(cx, 0x203, ~0x00009870u, "depth export, shader coverage or ordered fragment execution");
    zero(cx, 0x2dc, ~0x0001ff00u, "alpha-to-coverage");
    zero(cx, 0x2f8, ~0u, "multisampling or coverage conversion");
    zero(cx, 0x292, ~2u, "scan conversion mode");
    zero(cx, 0x293, ~0x06003fffu, "sample iteration, primitive discard or out-of-order rasterization");
    zero(cx, 0x80, ~0u, "window offset");
    zero(cx, 0x8d, ~0x01ff01ffu, "reserved PA_SU_HARDWARE_SCREEN_OFFSET bits");
    Require(read(cx, 0x83) == 0xffffu, "clip rectangles are unsupported");
    Require((read(cx, 0x8c) & 0xfu) == 0xau, "nonstandard triangle edge rules are unsupported");
    Require(read(cx, 0x2f9) == 0x2du, "nonstandard pixel center or vertex quantization is unsupported");
    Require(read(cx, 0x313) == 0x6000u, "conservative rasterization is unsupported");
    Require(read(cx, 0x30e) == 0xffffffffu && read(cx, 0x30f) == 0xffffffffu, "sample masks are unsupported");
    const auto viewportControl = read(cx, 0x206);
    if (viewportControl != 0x43fu) {
        std::ostringstream message;
        message << "AGC graphics: PA_CL_VTE_CNTL=0x" << std::hex << viewportControl << ": expected 0x43f for homogeneous positions and all viewport transforms; pre-divided coordinates, reciprocal W or disabled transforms are unsupported";
        throw std::runtime_error(message.str());
    }
    zero(cx, 0x204, ~0x80000u, "unsupported PA_CL_CLIP_CNTL flags");
    result.negativeOneToOne = (read(cx, 0x204) & 0x80000u) == 0;
    const auto raster = read(cx, 0x205);
    Require((raster & ~0x7u) == 0 || (raster & ~0x7u) == 0x240u, "polygon mode, depth bias, provoking vertex or nonstandard rasterization is unsupported");
    result.cullMode = ((raster & 1u) != 0 ? VK_CULL_MODE_FRONT_BIT : 0u) | ((raster & 2u) != 0 ? VK_CULL_MODE_BACK_BIT : 0u);
    if (result.rectList) result.cullMode = VK_CULL_MODE_NONE;
    result.frontFace = (raster & 4u) != 0 ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
    const auto targetMask = read(cx, 0x8e);
    const auto shaderMask = read(cx, 0x8f);
    if ((targetMask & ~0xfu) != 0 || (shaderMask & ~0xfu) != 0) {
        std::ostringstream message;
        message << "AGC graphics: only color target zero is supported: CB_TARGET_MASK=0x" << std::hex << targetMask << ", CB_SHADER_MASK=0x" << shaderMask;
        throw std::runtime_error(message.str());
    }
    result.hasColorTarget = targetMask != 0;
    Require(!result.hasColorTarget || shaderMask == 0xfu, "partial shader color exports are unsupported");
    Require(read(cx, 0x202) == 0xcc0010u, "only normal color rendering with copy ROP is supported");
    zero(cx, 0x1c4, ~0u, "depth or sample-mask export");
    const auto exportFormat = read(cx, 0x1c5);
    Require(exportFormat == 4 || exportFormat == 9, "only FP16_ABGR or 32_ABGR color export is supported");
    Require(read(cx, 0x1c3) == 4, "additional position exports are unsupported");
    if (result.hasColorTarget) {
        const auto info = read(cx, 0x31c);
        const auto number = (info >> 8u) & 7u;
        const auto swap = (info >> 11u) & 3u;
        Require(((info >> 2u) & 0x1fu) == 10 && (number == 0 || number == 6) && swap <= 1, "unsupported color format or component order");
        Require((info & ~0x00029f7cu) == 0, "color compression, DCC, endian conversion, nonstandard rounding or color optimization is unsupported");
        Require((info & 0x8000u) != 0, "unclamped normalized color is unsupported");
        zero(cx, 0x31b, ~0u, "color mip or array view");
        zero(cx, 0x31d, ~0u, "color samples, fragments or destination alpha override");
        const auto attrib2 = read(cx, 0x3b0);
        Require((attrib2 >> 28u) == 0, "mipmapped render targets are unsupported");
        const auto attrib3 = read(cx, 0x3b8);
        result.color.tileMode = DecodeColorTileMode(attrib3);
        result.color.extent = {((attrib2 >> 14u) & 0x3fffu) + 1u, (attrib2 & 0x3fffu) + 1u};
        const ColorTargetLayout colorLayout(result.color.extent.width, result.color.extent.height, result.color.tileMode);
        const auto high = read(cx, 0x390);
        Require((high & ~0xffu) == 0, "invalid color address extension");
        result.color.address = (static_cast<std::uint64_t>(high) << 40u) | (static_cast<std::uint64_t>(read(cx, 0x318)) << 8u);
        result.color.bytes = colorLayout.Bytes();
        GuestMemory::CheckGpuRange(reinterpret_cast<const void*>(result.color.address), result.color.bytes, colorLayout.Alignment(), true);
        result.color.format = swap == 0 ? (number == 0 ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_SRGB) : (number == 0 ? VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_B8G8R8A8_SRGB);
        result.color.componentMapping = 0xe4u;
        result.renderExtent = result.color.extent;
    } else {
        const auto screenBottomRight = read(cx, 0xd);
        result.renderExtent = {screenBottomRight & 0xffffu, screenBottomRight >> 16u};
        Require(result.renderExtent.width != 0 && result.renderExtent.height != 0, "empty framebuffer extent for a draw without color writes");
    }
    const auto xs = readFloat(cx, 0x10f);
    const auto xo = readFloat(cx, 0x110);
    const auto ys = readFloat(cx, 0x111);
    const auto yo = readFloat(cx, 0x112);
    const auto zs = readFloat(cx, 0x113);
    const auto zo = readFloat(cx, 0x114);
    const auto minDepth = result.negativeOneToOne ? zo - zs : zo;
    const auto maxDepth = zo + zs;
    if (!(xs > 0 && ys != 0 && std::isfinite(minDepth) && std::isfinite(maxDepth))) {
        std::ostringstream message;
        message << "AGC graphics: unsupported viewport transform: scale=(" << xs << ", " << ys << ", " << zs << "), offset=(" << xo << ", " << yo << ", " << zo << "), depth=(" << minDepth << ", " << maxDepth << "), negativeOneToOne=" << result.negativeOneToOne;
        throw std::runtime_error(message.str());
    }
    Require(readFloat(cx, 0xb4) <= readFloat(cx, 0xb5), "inverted viewport depth clamp bounds");
    result.viewport = {xo - xs, yo - ys, 2 * xs, 2 * ys, minDepth, maxDepth};
    result.scissor = {{0, 0}, result.renderExtent};
    intersect(result.scissor, cx, 0xc, true);
    intersect(result.scissor, cx, 0x81, false);
    intersect(result.scissor, cx, 0x90, false);
    if ((read(cx, 0x292) & 2u) != 0) intersect(result.scissor, cx, 0x94, false);
    if (result.hasColorTarget) {
        const auto blend = read(cx, 0x1e0);
        Require((blend & 0x0000e000u) == 0, "reserved blend control bits");
        result.blend.colorWriteMask = targetMask;
        result.blend.blendEnable = (blend >> 30u) & 1u;
        if (result.blend.blendEnable) {
            Require((read(cx, 0x31c) & 0x10000u) == 0, "blend bypass conflicts with enabled blending");
            result.blend.srcColorBlendFactor = blendFactor(blend & 0x1fu);
            result.blend.dstColorBlendFactor = blendFactor((blend >> 8u) & 0x1fu);
            result.blend.colorBlendOp = blendOp((blend >> 5u) & 7u);
            const auto alpha = (blend & 0x20000000u) != 0 ? blend >> 16u : blend;
            result.blend.srcAlphaBlendFactor = blendFactor(alpha & 0x1fu);
            result.blend.dstAlphaBlendFactor = blendFactor((alpha >> 8u) & 0x1fu);
            result.blend.alphaBlendOp = blendOp((alpha >> 5u) & 7u);
            for (std::uint32_t i = 0; i < 4; ++i) result.blendConstants[i] = readFloat(cx, 0x105 + i);
        }
    }
    ApplyConditionalColorWrites(result);
    ValidateDepthClear(result);
    return result;
}

VkCompareOp DecodeCompareOp(std::uint32_t value) {
    Require(value <= 7, "unsupported compare function " + std::to_string(value));
    // AGC NEVER..ALWAYS (0..7) is numerically identical to VK_COMPARE_OP_NEVER..ALWAYS.
    return static_cast<VkCompareOp>(value);
}

VkStencilOp DecodeStencilOp(std::uint32_t value, std::uint32_t writeMask, std::uint32_t opValue) {
    // A zero write mask turns every operation into a keep, whatever its encoding.
    if (writeMask == 0) return VK_STENCIL_OP_KEEP;
    switch (value) {
        case 0: return VK_STENCIL_OP_KEEP;
        case 1: return VK_STENCIL_OP_ZERO;
        case 3:  // STENCIL_REPLACE_TEST
        case 4:  // STENCIL_REPLACE_OP (value checked against the test value by the caller)
            return VK_STENCIL_OP_REPLACE;
        // ADD/SUB add or subtract STENCILOPVAL; Vulkan steps by exactly one.
        case 5:
        case 6:
        case 8:
        case 9:
            Require(opValue == 1, "stencil add/subtract with an op value other than 1 is unsupported");
            return value == 5 ? VK_STENCIL_OP_INCREMENT_AND_CLAMP : value == 6 ? VK_STENCIL_OP_DECREMENT_AND_CLAMP : value == 8 ? VK_STENCIL_OP_INCREMENT_AND_WRAP : VK_STENCIL_OP_DECREMENT_AND_WRAP;
        case 7: return VK_STENCIL_OP_INVERT;
        case 12:  // STENCIL_XOR: flips the written bits that are set in STENCILOPVAL
            if ((writeMask & opValue) == 0) return VK_STENCIL_OP_KEEP;
            Require((writeMask & ~opValue) == 0, "stencil XOR that flips only some of the written bits is unsupported");
            return VK_STENCIL_OP_INVERT;
        default: throw std::runtime_error("AGC graphics: unsupported stencil operation " + std::to_string(value));
    }
}

VkPipelineDepthStencilStateCreateInfo ToVulkan(const DepthStencilState& state) {
    VkPipelineDepthStencilStateCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    info.depthTestEnable = state.depthTestEnable;
    info.depthWriteEnable = state.depthWriteEnable;
    info.depthCompareOp = state.depthCompareOp;
    info.depthBoundsTestEnable = state.depthBoundsTestEnable;
    info.stencilTestEnable = state.stencilTestEnable;
    info.front = state.front;
    info.back = state.back;
    info.minDepthBounds = state.minDepthBounds;
    info.maxDepthBounds = state.maxDepthBounds;
    return info;
}

VkPipelineDepthStencilStateCreateInfo ToVulkan(const DepthStencilState& state, const DepthTarget& target, bool depthBoundsSupported) {
    auto info = ToVulkan(state);
    // A depth aspect that is absent, or cleared by this draw (the hardware writes the clear
    // value instead of testing), neither tests nor writes. Vulkan would also ignore state for
    // a missing aspect, but dropping it keeps the pipeline key minimal and the intent explicit.
    if (!target.HasDepth() || target.clearDepth) {
        info.depthTestEnable = VK_FALSE;
        info.depthWriteEnable = VK_FALSE;
        info.depthBoundsTestEnable = VK_FALSE;
    }
    if (!target.hasStencil || target.clearStencil) {
        info.stencilTestEnable = VK_FALSE;
        info.front = info.back = {};
    }
    Require(!info.depthBoundsTestEnable || depthBoundsSupported, "depth bounds test requires VkPhysicalDeviceFeatures::depthBounds, which the device does not support");
    return info;
}

void DepthStencilReads(const DepthStencilState& state, const DepthTarget& target, bool& depth, bool& stencil) {
    depth = target.HasDepth() && !target.clearDepth && (state.depthTestEnable || state.depthBoundsTestEnable);
    stencil = target.hasStencil && !target.clearStencil && state.stencilTestEnable;
}

}
