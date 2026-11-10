// Copyright 2026 The Dawn & Tint Authors
// SPDX-License-Identifier: BSD-3-Clause

// Translates captured WGSL requests without creating a Dawn instance, adapter, or device.
// Only GameEngineShaderRequest entries are inputs; cached phone-compiled shaders are ignored.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "src/dawn/native/BlobCache.h"
#include "src/dawn/native/CacheRequest.h"
#include "src/dawn/native/Serializable.h"
#include "src/dawn/native/ShaderModule.h"
#include "src/dawn/native/TintUtils.h"
#include "src/dawn/native/stream/BlobSource.h"
#include "tint/tint.h"

namespace dawn::native {
namespace {

constexpr char kRequestPrefix[] = "GameEngineShaderRequest";
constexpr char kRequestType[] = "SpirvCompilationRequest";
constexpr std::string_view kRequestMagic = "GESHREQ1";
constexpr std::string_view kPackMagic = "GESPV001";
constexpr size_t kMaxFileBytes = 32 * 1024 * 1024;
constexpr size_t kMaxPackBytes = 256 * 1024 * 1024;

// Keep this list byte-for-byte compatible with vulkan/ShaderModuleVk.cpp. Unsafe fields do not
// occupy bytes in the key and are deliberately left default-constructed when deserializing.
#define SPIRV_COMPILATION_REQUEST_MEMBERS(X)                                         \
    X(SingleShaderStage, stage)                                                      \
    X(ShaderModuleBase::ShaderModuleHash, shaderModuleHash)                          \
    X(UnsafeUnserializedValue<ShaderModuleBase::ScopedUseTintProgram>, inputProgram) \
    X(LimitsForCompilationRequest, limits)                                           \
    X(UnsafeUnserializedValue<LimitsForCompilationRequest>, adapterSupportedLimits)  \
    X(uint32_t, maxSubgroupSize)                                                     \
    X(uint32_t, minSubgroupSize)                                                     \
    X(uint32_t, maxComputeWorkgroupSubgroups)                                        \
    X(bool, usesSubgroupMatrix)                                                      \
    X(std::vector<SubgroupMatrixConfig>, subgroupMatrixConfig)                       \
    X(tint::spirv::writer::Options, tintOptions)                                     \
    X(UnsafeUnserializedValue<dawn::platform::Platform*>, platform)

DAWN_MAKE_CACHE_REQUEST(SpirvCompilationRequest, SPIRV_COMPILATION_REQUEST_MEMBERS);
#undef SPIRV_COMPILATION_REQUEST_MEMBERS

#define COMPILED_SPIRV_MEMBERS(X)                    \
    X(std::vector<uint32_t>, spirv)                  \
    X(std::optional<uint32_t>, explicitSubgroupSize) \
    X(Extent3D, workgroupSize)

DAWN_SERIALIZABLE(struct, CompiledSpirv, COMPILED_SPIRV_MEMBERS) {};
#undef COMPILED_SPIRV_MEMBERS

struct Statistics {
    size_t files = 0;
    size_t ignored = 0;
    size_t requests = 0;
    size_t duplicates = 0;
    size_t inputBytes = 0;
};

bool StartsWith(std::span<const std::byte> bytes, std::string_view prefix) {
    return bytes.size() >= prefix.size() &&
           std::memcmp(bytes.data(), prefix.data(), prefix.size()) == 0;
}

uint32_t ReadU32(std::span<const std::byte> bytes) {
    return std::to_integer<uint32_t>(bytes[0]) | (std::to_integer<uint32_t>(bytes[1]) << 8) |
           (std::to_integer<uint32_t>(bytes[2]) << 16) |
           (std::to_integer<uint32_t>(bytes[3]) << 24);
}

void WriteU32(std::ostream& output, uint32_t value) {
    const char bytes[] = {static_cast<char>(value), static_cast<char>(value >> 8),
                          static_cast<char>(value >> 16), static_cast<char>(value >> 24)};
    output.write(bytes, sizeof(bytes));
}

ResultOrError<std::vector<std::byte>> ReadFile(const std::filesystem::path& path) {
    std::error_code ec;
    uintmax_t size = std::filesystem::file_size(path, ec);
    DAWN_INVALID_IF(ec, "Cannot read file size: %s", path.string());
    DAWN_INVALID_IF(size < 4 || size > kMaxFileBytes, "Invalid .blob file size: %s",
                    path.string());
    std::vector<std::byte> bytes(static_cast<size_t>(size));
    std::ifstream input(path, std::ios::binary);
    DAWN_INVALID_IF(!input.read(reinterpret_cast<char*>(bytes.data()), bytes.size()),
                    "Cannot read .blob file: %s", path.string());
    return bytes;
}

ResultOrError<SpirvCompilationRequest> ReadRequest(std::span<const std::byte> key) {
    const auto marker = std::as_bytes(std::span(kRequestType));
    auto begin = std::search(key.begin(), key.end(), marker.begin(), marker.end());
    DAWN_INVALID_IF(begin == key.end(), "Synthetic key has no SpirvCompilationRequest marker");
    auto serialized = key.subspan(static_cast<size_t>(begin - key.begin()) + marker.size());
    std::vector<std::byte> requestBytes(serialized.begin(), serialized.end());
    stream::BlobSource source(Blob::Create(std::move(requestBytes)));
    SpirvCompilationRequest request;
    DAWN_TRY(request.VisitAll([&](auto&... members) { return StreamOut(&source, &members...); }));

    // Re-encoding rejects trailing bytes and any key schema or native-width mismatch. Dawn sorts
    // map and set fields during serialization, so this comparison is deterministic.
    stream::ByteVectorSink encoded;
    request.VisitAll([&](const auto&... members) { StreamIn(&encoded, members...); });
    DAWN_INVALID_IF(encoded.size() != serialized.size() ||
                        !std::equal(encoded.begin(), encoded.end(), serialized.begin()),
                    "Request key does not match this Dawn revision's serialization schema");
    DAWN_INVALID_IF(request.stage != SingleShaderStage::Vertex &&
                        request.stage != SingleShaderStage::Fragment &&
                        request.stage != SingleShaderStage::Compute,
                    "Request contains an invalid shader stage");
    DAWN_INVALID_IF(request.usesSubgroupMatrix,
                    "Offline compiler does not support subgroup-matrix shader requests");
    return request;
}

ResultOrError<CompiledSpirv> Compile(const SpirvCompilationRequest& request, std::string_view wgsl) {
    tint::Source::File file("captured.wgsl", wgsl);
    tint::wgsl::reader::Options parserOptions;
    parserOptions.allowed_features = tint::wgsl::AllowedFeatures::Everything();
    tint::Program program = tint::wgsl::reader::Parse(&file, parserOptions);
    DAWN_INVALID_IF(!program.IsValid(), "WGSL parsing failed: %s",
                    tint::diag::Formatter().Format(program.Diagnostics()).Plain());
    auto ir = tint::wgsl::reader::ProgramToLoweredIR(program);
    DAWN_INVALID_IF(ir != tint::Success, "WGSL lowering failed: %s", ir.Failure().reason);
    auto generated = tint::spirv::writer::Generate(ir.Get(), request.tintOptions);
    DAWN_INVALID_IF(generated != tint::Success, "SPIR-V generation failed: %s",
                    generated.Failure().reason);

    if (request.stage == SingleShaderStage::Compute) {
        // Adapter limits were intentionally omitted from the cache key. They only add advice to
        // validation errors; the serialized device limits decide whether compilation is valid.
        DAWN_TRY(ValidateComputeStageWorkgroupSize(
            generated->workgroup_info, false, request.maxSubgroupSize, request.limits,
            request.limits));
        if (generated->workgroup_info.subgroup_size.has_value()) {
            uint32_t subgroupSize = generated->workgroup_info.subgroup_size.value();
            DAWN_INVALID_IF(subgroupSize < request.minSubgroupSize ||
                                subgroupSize > request.maxSubgroupSize,
                            "Explicit subgroup size is outside the recorded device range");
            uint32_t invocations = generated->workgroup_info.x * generated->workgroup_info.y *
                                   generated->workgroup_info.z;
            DAWN_INVALID_IF(invocations / subgroupSize > request.maxComputeWorkgroupSubgroups,
                            "Workgroup exceeds the recorded device subgroup count limit");
        }
    }
    DAWN_TRY(ValidateSubgroupMatrixConfiguration(generated->subgroup_matrix_info,
                                                 request.subgroupMatrixConfig));
    DAWN_INVALID_IF(generated->spirv.empty(), "Tint generated no SPIR-V instructions");
    CompiledSpirv result;
    result.spirv = std::move(generated.Get().spirv);
    result.explicitSubgroupSize = generated->workgroup_info.subgroup_size;
    result.workgroupSize = {generated->workgroup_info.x, generated->workgroup_info.y,
                            generated->workgroup_info.z};
    return result;
}

MaybeError CompileFile(const std::filesystem::path& path,
                       std::map<std::vector<std::byte>, std::vector<std::byte>>* entries,
                       Statistics* stats,
                       size_t* packBytes) {
    std::vector<std::byte> bytes;
    DAWN_TRY_ASSIGN(bytes, ReadFile(path));
    stats->files++;
    stats->inputBytes += bytes.size();
    std::span<const std::byte> data(bytes);
    uint32_t keyLength = ReadU32(data.first(4));
    DAWN_INVALID_IF(keyLength > data.size() - 4, "Truncated .blob cache key");
    auto key = data.subspan(4, keyLength);
    if (!StartsWith(key, std::string_view(kRequestPrefix, sizeof(kRequestPrefix)))) {
        stats->ignored++;
        return {};
    }
    stats->requests++;
    key = key.subspan(sizeof(kRequestPrefix));
    auto value = data.subspan(4 + keyLength);
    size_t hashBytes = 0;
    if (!StartsWith(value, kRequestMagic)) {
        hashBytes = dawn::Sha3_224::kByteOutputLength;
        DAWN_INVALID_IF(value.size() < hashBytes ||
                            !StartsWith(value.subspan(hashBytes), kRequestMagic),
                        "Synthetic entry has no GESHREQ1 payload at offset 0 or 28");
        auto hash = dawn::Sha3_224::Hash(value.subspan(hashBytes));
        DAWN_INVALID_IF(std::memcmp(value.data(), hash.data(), hash.size()) != 0,
                        "Captured request SHA3-224 digest does not match its payload");
        value = value.subspan(hashBytes);
    }
    DAWN_INVALID_IF(value.size() < kRequestMagic.size() + 5, "Truncated GESHREQ1 payload");
    value = value.subspan(kRequestMagic.size());
    uint8_t hashValidation = std::to_integer<uint8_t>(value[0]);
    DAWN_INVALID_IF(hashValidation > 1 || ((hashBytes != 0) != (hashValidation == 1)),
                    "Captured request hash-validation flag does not match its storage wrapper");
    uint32_t wgslLength = ReadU32(value.subspan(1, 4));
    DAWN_INVALID_IF(wgslLength == 0 || wgslLength != value.size() - 5,
                    "Captured WGSL length does not match its payload");
    SpirvCompilationRequest request;
    DAWN_TRY_ASSIGN(request, ReadRequest(key));
    CompiledSpirv compiled;
    DAWN_TRY_ASSIGN(compiled,
                    Compile(request, std::string_view(reinterpret_cast<const char*>(value.data() + 5),
                                                      wgslLength)));
    Blob serialized = compiled.ToBlob();
    BlobCache cache(DawnCacheDeviceDescriptor{}, hashValidation == 1);
    Blob stored = cache.GenerateActualStoredBlobForTesting(
        std::span<const std::byte>(serialized.DataPtr(), serialized.Size()));
    std::vector<std::byte> cacheValue(stored.DataPtr(), stored.DataPtr() + stored.Size());
    std::vector<std::byte> cacheKey(key.begin(), key.end());
    auto existing = entries->find(cacheKey);
    if (existing != entries->end()) {
        DAWN_INVALID_IF(existing->second != cacheValue,
                        "Duplicate request key compiled to conflicting cache values");
        stats->duplicates++;
        return {};
    }
    size_t entryBytes = 8 + cacheKey.size() + cacheValue.size();
    DAWN_INVALID_IF(entryBytes > kMaxPackBytes - *packBytes,
                    "Compiled shader pack exceeds the 256 MiB limit");
    *packBytes += entryBytes;
    entries->emplace(std::move(cacheKey), std::move(cacheValue));
    return {};
}

MaybeError Run(const std::filesystem::path& directory, const std::filesystem::path& outputPath) {
    std::error_code ec;
    DAWN_INVALID_IF(!std::filesystem::is_directory(directory, ec) || ec,
                    "Requests path is not a readable directory");
    std::vector<std::filesystem::path> files;
    std::filesystem::recursive_directory_iterator it(directory, ec), end;
    DAWN_INVALID_IF(ec, "Cannot enumerate the requests directory");
    for (; it != end; it.increment(ec)) {
        DAWN_INVALID_IF(ec, "Cannot enumerate the requests directory");
        bool regular = it->is_regular_file(ec);
        DAWN_INVALID_IF(ec, "Cannot inspect a request-directory entry");
        if (regular && it->path().extension() == ".blob") {
            files.push_back(it->path());
        }
    }
    DAWN_INVALID_IF(ec, "Cannot enumerate the requests directory");
    std::sort(files.begin(), files.end());
    std::map<std::vector<std::byte>, std::vector<std::byte>> entries;
    Statistics stats;
    size_t packBytes = kPackMagic.size() + 4;
    for (const auto& path : files) {
        auto result = CompileFile(path, &entries, &stats, &packBytes);
        if (result.IsError()) {
            auto error = result.AcquireError();
            error->AppendContext("Compiling captured request %s", path.string());
            return error;
        }
    }
    DAWN_INVALID_IF(entries.empty(), "No synthetic WGSL shader requests were found");
    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    DAWN_INVALID_IF(!output, "Cannot create output pack: %s", outputPath.string());
    output.write(kPackMagic.data(), kPackMagic.size());
    WriteU32(output, static_cast<uint32_t>(entries.size()));
    for (const auto& [key, value] : entries) {
        WriteU32(output, static_cast<uint32_t>(key.size()));
        WriteU32(output, static_cast<uint32_t>(value.size()));
        output.write(reinterpret_cast<const char*>(key.data()), key.size());
        output.write(reinterpret_cast<const char*>(value.data()), value.size());
    }
    output.close();
    DAWN_INVALID_IF(!output, "Failed to write the complete shader pack");
    std::cout << "{\"sourceOrigin\":\"offlineCompile\",\"files\":" << stats.files
              << ",\"ignoredEntries\":" << stats.ignored << ",\"requests\":" << stats.requests
              << ",\"compiledEntries\":" << entries.size() << ",\"duplicateRequests\":"
              << stats.duplicates << ",\"inputBytes\":" << stats.inputBytes << ",\"packBytes\":"
              << packBytes << "}\n";
    return {};
}

}  // namespace
}  // namespace dawn::native

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: dawn_shader_compiler <requests-directory> <output-pack>\n";
        return 2;
    }
    tint::Initialize();
    auto result = dawn::native::Run(argv[1], argv[2]);
    tint::Shutdown();
    if (result.IsError()) {
        std::cerr << result.AcquireError()->GetFormattedMessage() << '\n';
        return 1;
    }
    return 0;
}
