/*
 * BDA (buffer device address) target and request-serialization contract tests, run before the
 * device checks in BdaDevice.cpp. Verifies each contract violation is rejected with its own
 * message; no Vulkan device needed.
 */
#include "BdaShader.hpp"
#include "ControlFlow/RequestSerializer.hpp"
#include "SpirvBackend/SpirvBda.hpp"
#include <array>

namespace {

template<typename TAction>
void reject(TAction action, const char* expected) {
    try { action(); }
    catch (const std::runtime_error& error) {
        AgcDriver::Graphics::Require(std::string(error.what()).find(expected) != std::string::npos, std::string("unexpected contract error: ") + error.what());
        return;
    }
    throw std::runtime_error(std::string("missing contract rejection: ") + expected);
}

}

void RunBdaContractTests() {
    using namespace ShaderRecompiler;
    const std::array<std::uint32_t, 3> capabilities{spv::CapabilityInt64, spv::CapabilityPhysicalStorageBufferAddresses, spv::CapabilityStorageBuffer8BitAccess};
    const std::array<std::string_view, 2> extensions{"SPV_KHR_physical_storage_buffer", "SPV_KHR_8bit_storage"};
    SpirvTargetOptions target{0x00401000u, 0x00010300u, 32, BdaAbi::Version, capabilities, extensions};
    IrProgram program;
    program.Resources().stage = IrShaderStage::Compute;
    program.Info().usesDma = true;
    ValidateBdaTarget(program, target);
    target.bdaAbiVersion = 0;
    reject([&] { ValidateBdaTarget(program, target); }, "ABI version");
    target.bdaAbiVersion = BdaAbi::Version;
    target.supportedCapabilities = {};
    reject([&] { ValidateBdaTarget(program, target); }, "capability");
    target.supportedCapabilities = capabilities;
    target.supportedExtensions = {};
    reject([&] { ValidateBdaTarget(program, target); }, "extension");
    target.supportedExtensions = extensions;
    auto& block = program.CreateBlock();
    block.AppendInstruction(&program.CreateValue(IrOpcode::Barrier, IrType::Void));
    program.BlockOrder().push_back(&block);
    reject([&] { ValidateBdaTarget(program, target); }, "workgroup barrier");
    RecompileRequest request{};
    request.target.bdaAbiVersion = BdaAbi::Version;
    request.target.supportedCapabilities = capabilities;
    request.target.supportedExtensions = extensions;
    request.target.fragmentShaderBarycentricEnabled = false;
    const RequestSerializer serializer;
    const auto encoded = serializer.Serialize(request);
    const auto decoded = serializer.Deserialize(encoded);
    AgcDriver::Graphics::Require(decoded.request.target.bdaAbiVersion == BdaAbi::Version && decoded.request.target.supportedCapabilities.size() == capabilities.size() && decoded.request.target.supportedExtensions[1] == extensions[1], "BDA request serialization changed target contract");
    // Deserialize checks the 'APS5' signature (0x41505335) before the version, so each rejection
    // needs its own blob. Little-endian u32 pairs, base64-encoded:
    //   signature 0x41505336 (wrong) + version 2  -> "invalid ... signature"
    //   signature 0x41505335 (right) + version 99 -> "unsupported ... serialization version"
    const std::string badSignature = "NlNQQQIAAAA=";
    const std::string badVersion = "NVNQQWMAAAA=";
    reject([&] { static_cast<void>(serializer.Deserialize(badSignature)); }, "signature");
    reject([&] { static_cast<void>(serializer.Deserialize(badVersion)); }, "serialization version");
}
