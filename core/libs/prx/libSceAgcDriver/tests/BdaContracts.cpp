// core/libs/prx/libSceAgcDriver/tests/BdaContracts.cpp
// Host-only contract tests for the buffer-device-address (BDA) shader ABI: the SPIR-V emitter's BDA
// target validation and the recompile-request serializer must reject malformed targets with a clear
// error instead of emitting modules the driver would mis-bind. Runs without a Vulkan device, before
// the device checks in BdaDevice.cpp; each contract violation (signature, version, target) is rejected
// and tested with its own message.
// Targets mirror VulkanDevice::Target: Vulkan 1.3 with SPIR-V 1.3 (docs/spec/gpu-driver.md).

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
    SpirvTargetOptions target{0x00403000u, 0x00010300u, 32, BdaAbi::Version, capabilities, extensions};
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
