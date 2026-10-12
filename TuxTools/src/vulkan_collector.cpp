/*
 * Tux-Tools - native Win32 edition
 *
 * Vulkan driver information collector.
 *
 * Loads vulkan-1.dll at runtime (so no import library is required), creates a
 * throw-away Vulkan instance and reports the loader version, available
 * instance layers/extensions and detailed properties of every physical device.
 */

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <windows.h>

#include "collectors.h"

#include <string>
#include <vector>
#include "i18n.h"

namespace {

std::wstring vendorName(uint32_t vendorID) {
    switch (vendorID) {
        case 0x1002: return L"AMD";
        case 0x1010: return L"ImgTec";
        case 0x10DE: return L"NVIDIA";
        case 0x13B5: return L"ARM";
        case 0x1AE0: return L"Google";
        case 0x5143: return L"Qualcomm";
        case 0x8086: return L"Intel";
        case 0x10000: return L"Khronos";
        case 0x10001: return L"Vivante";
        case 0x10002: return L"VeriSilicon";
        case 0x10003: return L"Kazan";
        case 0x10004: return L"Codeplay";
        case 0x10005: return L"Mesa";
        case 0x10006: return L"PoCL";
        case 0x10007: return L"Mobileye";
        default: return i18n::tr(L"未知厂商");
    }
}

std::wstring deviceTypeName(VkPhysicalDeviceType type) {
    switch (type) {
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return i18n::tr(L"集成显卡");
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return i18n::tr(L"独立显卡");
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return i18n::tr(L"虚拟显卡");
        case VK_PHYSICAL_DEVICE_TYPE_CPU: return L"CPU";
        default: return i18n::tr(L"其他");
    }
}

std::wstring driverIdName(VkDriverId id) {
    switch (id) {
        case VK_DRIVER_ID_AMD_PROPRIETARY: return i18n::tr(L"AMD 专有驱动");
        case VK_DRIVER_ID_AMD_OPEN_SOURCE: return i18n::tr(L"AMD 开源驱动");
        case VK_DRIVER_ID_MESA_RADV: return L"Mesa RADV";
        case VK_DRIVER_ID_NVIDIA_PROPRIETARY: return i18n::tr(L"NVIDIA 专有驱动");
        case VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS: return i18n::tr(L"Intel 专有驱动（Windows）");
        case VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA: return i18n::tr(L"Intel 开源驱动（Mesa）");
        case VK_DRIVER_ID_IMAGINATION_PROPRIETARY: return i18n::tr(L"Imagination 专有驱动");
        case VK_DRIVER_ID_QUALCOMM_PROPRIETARY: return i18n::tr(L"Qualcomm 专有驱动");
        case VK_DRIVER_ID_ARM_PROPRIETARY: return i18n::tr(L"ARM 专有驱动");
        case VK_DRIVER_ID_GOOGLE_SWIFTSHADER: return L"Google SwiftShader";
        case VK_DRIVER_ID_GGP_PROPRIETARY: return i18n::tr(L"GGP 专有驱动");
        case VK_DRIVER_ID_BROADCOM_PROPRIETARY: return i18n::tr(L"Broadcom 专有驱动");
        case VK_DRIVER_ID_MESA_LLVMPIPE: return L"Mesa llvmpipe";
        case VK_DRIVER_ID_MOLTENVK: return L"MoltenVK";
        case VK_DRIVER_ID_COREAVI_PROPRIETARY: return i18n::tr(L"CoreAVI 专有驱动");
        case VK_DRIVER_ID_JUICE_PROPRIETARY: return i18n::tr(L"Juice 专有驱动");
        case VK_DRIVER_ID_VERISILICON_PROPRIETARY: return i18n::tr(L"VeriSilicon 专有驱动");
        case VK_DRIVER_ID_MESA_TURNIP: return L"Mesa Turnip";
        case VK_DRIVER_ID_MESA_V3DV: return L"Mesa V3DV";
        case VK_DRIVER_ID_MESA_PANVK: return L"Mesa PanVK";
        case VK_DRIVER_ID_SAMSUNG_PROPRIETARY: return i18n::tr(L"Samsung 专有驱动");
        case VK_DRIVER_ID_MESA_VENUS: return L"Mesa Venus";
        case VK_DRIVER_ID_MESA_DOZEN: return L"Mesa Dozen";
        case VK_DRIVER_ID_MESA_NVK: return L"Mesa NVK";
        case VK_DRIVER_ID_IMAGINATION_OPEN_SOURCE_MESA: return i18n::tr(L"Imagination 开源驱动（Mesa）");
        case VK_DRIVER_ID_MESA_HONEYKRISP: return L"Mesa HoneyKrisp";
        case VK_DRIVER_ID_VULKAN_SC_EMULATION_ON_VULKAN: return i18n::tr(L"Vulkan SC 模拟层（基于 Vulkan）");
        default: return i18n::tr(L"未知驱动");
    }
}

struct Api {
    HMODULE dll = nullptr;
    PFN_vkGetInstanceProcAddr GetInstanceProcAddr = nullptr;

    PFN_vkEnumerateInstanceVersion EnumerateInstanceVersion = nullptr;
    PFN_vkCreateInstance CreateInstance = nullptr;
    PFN_vkDestroyInstance DestroyInstance = nullptr;
    PFN_vkEnumerateInstanceExtensionProperties EnumerateInstanceExtensionProperties = nullptr;
    PFN_vkEnumerateInstanceLayerProperties EnumerateInstanceLayerProperties = nullptr;
    PFN_vkEnumeratePhysicalDevices EnumeratePhysicalDevices = nullptr;
    PFN_vkGetPhysicalDeviceProperties GetPhysicalDeviceProperties = nullptr;
    PFN_vkGetPhysicalDeviceFeatures GetPhysicalDeviceFeatures = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties GetPhysicalDeviceMemoryProperties = nullptr;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties GetPhysicalDeviceQueueFamilyProperties = nullptr;
    PFN_vkEnumerateDeviceExtensionProperties EnumerateDeviceExtensionProperties = nullptr;
    PFN_vkGetPhysicalDeviceProperties2 GetPhysicalDeviceProperties2 = nullptr;

    bool load() {
        dll = LoadLibraryW(L"vulkan-1.dll");
        if (!dll) return false;
        GetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            reinterpret_cast<void*>(GetProcAddress(dll, "vkGetInstanceProcAddr")));
        if (!GetInstanceProcAddr) return false;
        EnumerateInstanceVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
            GetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"));
        CreateInstance = reinterpret_cast<PFN_vkCreateInstance>(
            GetInstanceProcAddr(nullptr, "vkCreateInstance"));
        EnumerateInstanceExtensionProperties =
            reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
                GetInstanceProcAddr(nullptr, "vkEnumerateInstanceExtensionProperties"));
        EnumerateInstanceLayerProperties = reinterpret_cast<PFN_vkEnumerateInstanceLayerProperties>(
            GetInstanceProcAddr(nullptr, "vkEnumerateInstanceLayerProperties"));
        return CreateInstance != nullptr;
    }

    template <typename T>
    T get(VkInstance instance, const char* name) {
        return reinterpret_cast<T>(GetInstanceProcAddr(instance, name));
    }
};

#define VK_FEATURE_LIST(X)         \
    X(robustBufferAccess)          \
    X(fullDrawIndexUint32)         \
    X(imageCubeArray)              \
    X(independentBlend)            \
    X(geometryShader)              \
    X(tessellationShader)          \
    X(sampleRateShading)           \
    X(dualSrcBlend)                \
    X(logicOp)                     \
    X(multiDrawIndirect)           \
    X(drawIndirectFirstInstance)   \
    X(depthClamp)                  \
    X(depthBiasClamp)              \
    X(fillModeNonSolid)            \
    X(depthBounds)                 \
    X(wideLines)                   \
    X(largePoints)                 \
    X(alphaToOne)                  \
    X(multiViewport)               \
    X(samplerAnisotropy)           \
    X(textureCompressionETC2)      \
    X(textureCompressionASTC_LDR)  \
    X(textureCompressionBC)        \
    X(occlusionQueryPrecise)       \
    X(pipelineStatisticsQuery)     \
    X(vertexPipelineStoresAndAtomics) \
    X(fragmentStoresAndAtomics)    \
    X(shaderTessellationAndGeometryPointSize) \
    X(shaderImageGatherExtended)   \
    X(shaderStorageImageExtendedFormats) \
    X(shaderStorageImageMultisample) \
    X(shaderStorageImageReadWithoutFormat) \
    X(shaderStorageImageWriteWithoutFormat) \
    X(shaderUniformBufferArrayDynamicIndexing) \
    X(shaderSampledImageArrayDynamicIndexing) \
    X(shaderStorageBufferArrayDynamicIndexing) \
    X(shaderStorageImageArrayDynamicIndexing) \
    X(shaderClipDistance)          \
    X(shaderCullDistance)          \
    X(shaderFloat64)               \
    X(shaderInt64)                 \
    X(shaderInt16)                 \
    X(shaderResourceResidency)     \
    X(shaderResourceMinLod)        \
    X(sparseBinding)               \
    X(sparseResidencyBuffer)       \
    X(sparseResidencyImage2D)      \
    X(sparseResidencyImage3D)      \
    X(sparseResidency2Samples)     \
    X(sparseResidency4Samples)     \
    X(sparseResidency8Samples)     \
    X(sparseResidency16Samples)    \
    X(sparseResidencyAliased)      \
    X(variableMultisampleRate)     \
    X(inheritedQueries)

void addFeatures(viewer::Report& report, const VkPhysicalDeviceFeatures& f) {
    viewer::Section& s = report.addSection(i18n::tr(L"设备特性（已支持）"));
#define X(name) \
    if (f.name == VK_TRUE) s.fields.push_back({viewer::toWide(#name), i18n::tr(L"支持")});
    VK_FEATURE_LIST(X)
#undef X
    if (s.fields.empty()) s.fields.push_back({i18n::tr(L"（无）"), L""});
}

void addLimits(viewer::Report& report, const VkPhysicalDeviceLimits& l) {
    viewer::Section& s = report.addSection(i18n::tr(L"设备限制"));
#define ADD(name) s.fields.push_back({viewer::toWide(#name), viewer::formatUInt(l.name)});
    ADD(maxImageDimension1D)
    ADD(maxImageDimension2D)
    ADD(maxImageDimension3D)
    ADD(maxImageDimensionCube)
    ADD(maxImageArrayLayers)
    ADD(maxTexelBufferElements)
    ADD(maxUniformBufferRange)
    ADD(maxStorageBufferRange)
    ADD(maxPushConstantsSize)
    ADD(maxMemoryAllocationCount)
    ADD(maxSamplerAllocationCount)
    ADD(bufferImageGranularity)
    ADD(sparseAddressSpaceSize)
    ADD(maxBoundDescriptorSets)
    ADD(maxPerStageDescriptorSamplers)
    ADD(maxPerStageDescriptorUniformBuffers)
    ADD(maxPerStageDescriptorStorageBuffers)
    ADD(maxPerStageDescriptorSampledImages)
    ADD(maxPerStageDescriptorStorageImages)
    ADD(maxPerStageDescriptorInputAttachments)
    ADD(maxPerStageResources)
    ADD(maxDescriptorSetSamplers)
    ADD(maxDescriptorSetUniformBuffers)
    ADD(maxDescriptorSetUniformBuffersDynamic)
    ADD(maxDescriptorSetStorageBuffers)
    ADD(maxDescriptorSetStorageBuffersDynamic)
    ADD(maxDescriptorSetSampledImages)
    ADD(maxDescriptorSetStorageImages)
    ADD(maxDescriptorSetInputAttachments)
    ADD(maxVertexInputAttributes)
    ADD(maxVertexInputBindings)
    ADD(maxVertexInputAttributeOffset)
    ADD(maxVertexInputBindingStride)
    ADD(maxVertexOutputComponents)
    ADD(maxTessellationGenerationLevel)
    ADD(maxTessellationPatchSize)
    ADD(maxTessellationControlPerVertexInputComponents)
    ADD(maxTessellationControlPerVertexOutputComponents)
    ADD(maxTessellationControlPerPatchOutputComponents)
    ADD(maxTessellationControlTotalOutputComponents)
    ADD(maxTessellationEvaluationInputComponents)
    ADD(maxTessellationEvaluationOutputComponents)
    ADD(maxGeometryShaderInvocations)
    ADD(maxGeometryInputComponents)
    ADD(maxGeometryOutputComponents)
    ADD(maxGeometryOutputVertices)
    ADD(maxGeometryTotalOutputComponents)
    ADD(maxFragmentInputComponents)
    ADD(maxFragmentOutputAttachments)
    ADD(maxFragmentDualSrcAttachments)
    ADD(maxFragmentCombinedOutputResources)
    ADD(maxComputeSharedMemorySize)
    ADD(maxComputeWorkGroupCount[0])
    ADD(maxComputeWorkGroupCount[1])
    ADD(maxComputeWorkGroupCount[2])
    ADD(maxComputeWorkGroupInvocations)
    ADD(maxComputeWorkGroupSize[0])
    ADD(maxComputeWorkGroupSize[1])
    ADD(maxComputeWorkGroupSize[2])
    ADD(subPixelPrecisionBits)
    ADD(subTexelPrecisionBits)
    ADD(mipmapPrecisionBits)
    ADD(maxDrawIndexedIndexValue)
    ADD(maxDrawIndirectCount)
    ADD(maxSamplerLodBias)
    ADD(maxSamplerAnisotropy)
    ADD(maxViewportDimensions[0])
    ADD(maxViewportDimensions[1])
    ADD(viewportBoundsRange[0])
    ADD(viewportBoundsRange[1])
    ADD(minMemoryMapAlignment)
    ADD(minTexelBufferOffsetAlignment)
    ADD(minUniformBufferOffsetAlignment)
    ADD(minStorageBufferOffsetAlignment)
    ADD(minTexelOffset)
    ADD(maxTexelOffset)
    ADD(minTexelGatherOffset)
    ADD(maxTexelGatherOffset)
    ADD(minInterpolationOffset)
    ADD(maxInterpolationOffset)
    ADD(subPixelInterpolationOffsetBits)
    ADD(maxFramebufferWidth)
    ADD(maxFramebufferHeight)
    ADD(maxFramebufferLayers)
    ADD(framebufferColorSampleCounts)
    ADD(framebufferDepthSampleCounts)
    ADD(framebufferStencilSampleCounts)
    ADD(framebufferNoAttachmentsSampleCounts)
    ADD(maxColorAttachments)
    ADD(sampledImageColorSampleCounts)
    ADD(sampledImageIntegerSampleCounts)
    ADD(sampledImageDepthSampleCounts)
    ADD(sampledImageStencilSampleCounts)
    ADD(storageImageSampleCounts)
    ADD(maxSampleMaskWords)
    ADD(timestampComputeAndGraphics)
    ADD(timestampPeriod)
    ADD(maxClipDistances)
    ADD(maxCullDistances)
    ADD(maxCombinedClipAndCullDistances)
    ADD(discreteQueuePriorities)
    ADD(pointSizeRange[0])
    ADD(pointSizeRange[1])
    ADD(lineWidthRange[0])
    ADD(lineWidthRange[1])
    ADD(pointSizeGranularity)
    ADD(lineWidthGranularity)
    ADD(strictLines)
    ADD(standardSampleLocations)
    ADD(optimalBufferCopyOffsetAlignment)
    ADD(optimalBufferCopyRowPitchAlignment)
    ADD(nonCoherentAtomSize)
#undef ADD
}

std::wstring queueFlagsToString(VkQueueFlags flags) {
    std::wstring s;
    auto add = [&s](const wchar_t* name) {
        if (!s.empty()) s += L" | ";
        s += name;
    };
    if (flags & VK_QUEUE_GRAPHICS_BIT) add(L"GRAPHICS");
    if (flags & VK_QUEUE_COMPUTE_BIT) add(L"COMPUTE");
    if (flags & VK_QUEUE_TRANSFER_BIT) add(L"TRANSFER");
    if (flags & VK_QUEUE_SPARSE_BINDING_BIT) add(L"SPARSE_BINDING");
    if (flags & VK_QUEUE_PROTECTED_BIT) add(L"PROTECTED");
    if (s.empty()) s = L"0";
    return s;
}

std::wstring memoryPropertyFlagsToString(VkMemoryPropertyFlags flags) {
    std::wstring s;
    auto add = [&s](const wchar_t* name) {
        if (!s.empty()) s += L" | ";
        s += name;
    };
    if (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) add(L"DEVICE_LOCAL");
    if (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) add(L"HOST_VISIBLE");
    if (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) add(L"HOST_COHERENT");
    if (flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) add(L"HOST_CACHED");
    if (flags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) add(L"LAZILY_ALLOCATED");
    if (flags & VK_MEMORY_PROPERTY_PROTECTED_BIT) add(L"PROTECTED");
    if (s.empty()) s = L"0";
    return s;
}

void addInstanceInfo(Api& api, viewer::Report& report, uint32_t loaderVersion,
                      bool hasLoaderVersion) {
    viewer::Section& loader = report.addSection(i18n::tr(L"加载器"));
    if (hasLoaderVersion) {
        loader.fields.push_back({i18n::tr(L"Vulkan 加载器版本"),
            viewer::formatVersion(VK_API_VERSION_VARIANT(loaderVersion),
                                  VK_API_VERSION_MAJOR(loaderVersion),
                                  VK_API_VERSION_MINOR(loaderVersion),
                                  VK_API_VERSION_PATCH(loaderVersion))});
    } else {
        loader.fields.push_back({i18n::tr(L"Vulkan 加载器版本"), i18n::tr(L"1.0（加载器未上报版本）")});
    }

    uint32_t count = 0;
    if (api.EnumerateInstanceExtensionProperties(nullptr, &count, nullptr) == VK_SUCCESS && count) {
        std::vector<VkExtensionProperties> ext(count);
        api.EnumerateInstanceExtensionProperties(nullptr, &count, ext.data());
        viewer::Section& s = report.addSection(i18n::tr(L"实例扩展"));
        s.fields.push_back({i18n::tr(L"数量"), viewer::formatUInt(count)});
        for (const auto& e : ext) {
            s.fields.push_back({viewer::toWide(e.extensionName),
                                i18n::tr(L"规范版本 ") + viewer::formatUInt(e.specVersion)});
        }
    }

    count = 0;
    if (api.EnumerateInstanceLayerProperties(&count, nullptr) == VK_SUCCESS && count) {
        std::vector<VkLayerProperties> layers(count);
        api.EnumerateInstanceLayerProperties(&count, layers.data());
        viewer::Section& s = report.addSection(i18n::tr(L"实例层"));
        for (const auto& l : layers) {
            s.fields.push_back({viewer::toWide(l.layerName),
                                i18n::tr(L"规范版本 ") + viewer::formatUInt(l.specVersion) +
                                    i18n::tr(L"，实现版本 ") + viewer::formatUInt(l.implementationVersion)});
        }
    }
}

}  // namespace

namespace viewer {

Report collectVulkan() {
    Report report;

    Api api;
    if (!api.load()) {
        report.description = i18n::tr(L"Vulkan - 未找到加载器");
        report.addSection(L"Vulkan");
        report.add(i18n::tr(L"状态"), i18n::tr(L"未在当前系统上找到 vulkan-1.dll（Vulkan 加载器）。"));
        return report;
    }

    uint32_t loaderVersion = VK_API_VERSION_1_0;
    bool hasLoaderVersion = false;
    if (api.EnumerateInstanceVersion) {
        if (api.EnumerateInstanceVersion(&loaderVersion) == VK_SUCCESS) hasLoaderVersion = true;
    }

    addInstanceInfo(api, report, loaderVersion, hasLoaderVersion);

    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Tux-Tools";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Tux-Tools";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = loaderVersion;

    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    VkInstance instance = VK_NULL_HANDLE;
    VkResult res = api.CreateInstance(&createInfo, nullptr, &instance);
    if (res != VK_SUCCESS || instance == VK_NULL_HANDLE) {
        report.description = i18n::tr(L"Vulkan - 实例创建失败");
        report.addSection(L"Vulkan");
        report.add(i18n::tr(L"状态"), i18n::tr(L"创建 Vulkan 实例失败（VkResult ") + formatInt(res) + i18n::tr(L"）。"));
        return report;
    }

    api.DestroyInstance = api.get<PFN_vkDestroyInstance>(instance, "vkDestroyInstance");
    api.EnumeratePhysicalDevices =
        api.get<PFN_vkEnumeratePhysicalDevices>(instance, "vkEnumeratePhysicalDevices");
    api.GetPhysicalDeviceProperties =
        api.get<PFN_vkGetPhysicalDeviceProperties>(instance, "vkGetPhysicalDeviceProperties");
    api.GetPhysicalDeviceFeatures =
        api.get<PFN_vkGetPhysicalDeviceFeatures>(instance, "vkGetPhysicalDeviceFeatures");
    api.GetPhysicalDeviceMemoryProperties = api.get<PFN_vkGetPhysicalDeviceMemoryProperties>(
        instance, "vkGetPhysicalDeviceMemoryProperties");
    api.GetPhysicalDeviceQueueFamilyProperties = api.get<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
        instance, "vkGetPhysicalDeviceQueueFamilyProperties");
    api.EnumerateDeviceExtensionProperties = api.get<PFN_vkEnumerateDeviceExtensionProperties>(
        instance, "vkEnumerateDeviceExtensionProperties");
    api.GetPhysicalDeviceProperties2 = api.get<PFN_vkGetPhysicalDeviceProperties2>(
        instance, "vkGetPhysicalDeviceProperties2");

    uint32_t deviceCount = 0;
    api.EnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        report.description = i18n::tr(L"Vulkan - 无可用设备");
        report.addSection(i18n::tr(L"Vulkan 设备"));
        report.add(i18n::tr(L"状态"), i18n::tr(L"未找到任何 Vulkan 物理设备。"));
        if (api.DestroyInstance) api.DestroyInstance(instance, nullptr);
        return report;
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    api.EnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    for (uint32_t i = 0; i < deviceCount; ++i) {
        VkPhysicalDeviceProperties props = {};
        api.GetPhysicalDeviceProperties(devices[i], &props);
        if (i == 0) {
            report.description = L"Vulkan " + formatVersion(
                VK_API_VERSION_VARIANT(props.apiVersion), VK_API_VERSION_MAJOR(props.apiVersion),
                VK_API_VERSION_MINOR(props.apiVersion), VK_API_VERSION_PATCH(props.apiVersion)) +
                L" - " + toWide(props.deviceName);
        }

        std::wstring header = i18n::tr(L"设备 ") + formatUInt(i) + i18n::tr(L"：") + toWide(props.deviceName);
        report.addSection(header);
        report.add(i18n::tr(L"设备名称"), toWide(props.deviceName));
        report.add(i18n::tr(L"设备类型"), deviceTypeName(props.deviceType));
        report.add(i18n::tr(L"厂商"), vendorName(props.vendorID) + L" (" + formatHex(props.vendorID) + L")");
        report.add(i18n::tr(L"厂商 ID"), formatHex(props.vendorID));
        report.add(i18n::tr(L"设备 ID"), formatHex(props.deviceID));
        report.add(i18n::tr(L"API 版本"), formatVersion(VK_API_VERSION_VARIANT(props.apiVersion),
                                                 VK_API_VERSION_MAJOR(props.apiVersion),
                                                 VK_API_VERSION_MINOR(props.apiVersion),
                                                 VK_API_VERSION_PATCH(props.apiVersion)));
        report.add(i18n::tr(L"驱动版本（原始值）"), formatHex(props.driverVersion));

        if (props.vendorID == 0x10DE) {
            report.add(i18n::tr(L"驱动版本（NVIDIA）"),
                formatUInt((props.driverVersion >> 22) & 0x3ff) + L"." +
                formatUInt((props.driverVersion >> 14) & 0x0ff) + L"." +
                formatUInt((props.driverVersion >> 6) & 0x0ff) + L"." +
                formatUInt(props.driverVersion & 0x3f));
        } else if (props.vendorID == 0x8086) {
            report.add(i18n::tr(L"驱动版本（Intel）"),
                formatUInt((props.driverVersion >> 14) & 0x3ffff) + L"." +
                formatUInt(props.driverVersion & 0x3fff));
        }

        if (api.GetPhysicalDeviceProperties2) {
            VkPhysicalDeviceDriverProperties driverProps = {};
            driverProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
            VkPhysicalDeviceProperties2 props2 = {};
            props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            props2.pNext = &driverProps;
            api.GetPhysicalDeviceProperties2(devices[i], &props2);
            report.add(i18n::tr(L"驱动名称"), driverIdName(driverProps.driverID));
            report.add(i18n::tr(L"驱动（上报名称）"), toWide(driverProps.driverName));
            report.add(i18n::tr(L"驱动信息"), toWide(driverProps.driverInfo));
            report.add(i18n::tr(L"一致性规范版本"),
                formatUInt(driverProps.conformanceVersion.major) + L"." +
                formatUInt(driverProps.conformanceVersion.minor) + L"." +
                formatUInt(driverProps.conformanceVersion.subminor) + L"." +
                formatUInt(driverProps.conformanceVersion.patch));
        }

        addLimits(report, props.limits);

        VkPhysicalDeviceFeatures features = {};
        api.GetPhysicalDeviceFeatures(devices[i], &features);
        addFeatures(report, features);

        uint32_t queueCount = 0;
        api.GetPhysicalDeviceQueueFamilyProperties(devices[i], &queueCount, nullptr);
        if (queueCount) {
            std::vector<VkQueueFamilyProperties> queues(queueCount);
            api.GetPhysicalDeviceQueueFamilyProperties(devices[i], &queueCount, queues.data());
            Section& s = report.addSection(i18n::tr(L"队列族"));
            for (uint32_t q = 0; q < queueCount; ++q) {
                s.fields.push_back({i18n::tr(L"队列族 ") + formatUInt(q),
                    queueFlagsToString(queues[q].queueFlags) +
                        i18n::tr(L"，数量 ") + formatUInt(queues[q].queueCount) +
                        i18n::tr(L"，timestampValidBits ") + formatUInt(queues[q].timestampValidBits)});
            }
        }

        VkPhysicalDeviceMemoryProperties mem = {};
        api.GetPhysicalDeviceMemoryProperties(devices[i], &mem);
        Section& heaps = report.addSection(i18n::tr(L"内存堆"));
        for (uint32_t h = 0; h < mem.memoryHeapCount; ++h) {
            std::wstring flags = (mem.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                                     ? L"DEVICE_LOCAL"
                                     : L"";
            heaps.fields.push_back({i18n::tr(L"堆 ") + formatUInt(h),
                                    formatUInt(mem.memoryHeaps[h].size / (1024ull * 1024ull)) +
                                        L" MiB " + flags});
        }
        Section& types = report.addSection(i18n::tr(L"内存类型"));
        for (uint32_t t = 0; t < mem.memoryTypeCount; ++t) {
            types.fields.push_back({i18n::tr(L"类型 ") + formatUInt(t),
                                    i18n::tr(L"堆 ") + formatUInt(mem.memoryTypes[t].heapIndex) +
                                        L", " + memoryPropertyFlagsToString(mem.memoryTypes[t].propertyFlags)});
        }

        uint32_t extCount = 0;
        api.EnumerateDeviceExtensionProperties(devices[i], nullptr, &extCount, nullptr);
        if (extCount) {
            std::vector<VkExtensionProperties> exts(extCount);
            api.EnumerateDeviceExtensionProperties(devices[i], nullptr, &extCount, exts.data());
            Section& s = report.addSection(i18n::tr(L"设备扩展（") + formatUInt(extCount) + i18n::tr(L"）"));
            for (const auto& e : exts) {
                s.fields.push_back({toWide(e.extensionName),
                                    i18n::tr(L"规范版本 ") + formatUInt(e.specVersion)});
            }
        }
    }

    if (api.DestroyInstance) api.DestroyInstance(instance, nullptr);
    return report;
}

}  // namespace viewer
