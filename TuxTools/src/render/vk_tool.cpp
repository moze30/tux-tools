/*
 * Tux-Tools - Vulkan driver test
 *
 * A rotating cube, the content the Vulkan ecosystem uses as its smoke test
 * (vkcube, from Khronos' Vulkan-Tools, Apache-2.0). The precompiled SPIR-V it
 * ships with is reused verbatim - see vk_cube.vert.inc / vk_cube.frag.inc - so
 * no shader compiler is needed at build time.
 *
 * What makes this exercise a different path from every other backend here:
 *
 *   - it is the only backend with a swapchain that has to be recreated when
 *     the surface goes out of date
 *   - Vulkan is loaded from vulkan-1.dll at run time, and every entry point is
 *     resolved through vkGetInstanceProcAddr, exactly as the capability viewer
 *     does. Nothing links against the loader, so the executable still imports
 *     only Windows system DLLs and still runs on a machine with no Vulkan.
 *
 * The original draws its texture from a PPM image file. An image asset would
 * violate the project's rule that all test content is generated in code, so the
 * texture here is synthesised: six flat colour tiles, one per cube face, with a
 * soft vignette so the faces stay distinguishable while the cube turns.
 */

#include "render_tool.h"
#include "test_content.h"

#define VK_USE_PLATFORM_WIN32_KHR 1
#include <vulkan/vulkan.h>

#include <cmath>
#include <cstring>
#include <vector>

namespace render {

namespace {

bool ok(VkResult r) { return r == VK_SUCCESS; }

// --- the loader -----------------------------------------------------------
//
// Every entry point below is fetched at run time. Linking against vulkan-1.lib
// would put the loader in the import table, which the project forbids: the
// executable must import only Windows system DLLs, and it must still start on a
// machine that has no Vulkan driver at all.

struct VkApi {
    HMODULE dll = nullptr;
    PFN_vkGetInstanceProcAddr GetInstanceProcAddr = nullptr;

    PFN_vkCreateInstance CreateInstance = nullptr;
    PFN_vkEnumerateInstanceExtensionProperties EnumerateInstanceExtensionProperties = nullptr;

    PFN_vkDestroyInstance DestroyInstance = nullptr;
    PFN_vkEnumeratePhysicalDevices EnumeratePhysicalDevices = nullptr;
    PFN_vkGetPhysicalDeviceProperties GetPhysicalDeviceProperties = nullptr;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties GetPhysicalDeviceQueueFamilyProperties = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties GetPhysicalDeviceMemoryProperties = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR GetPhysicalDeviceSurfaceSupportKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR GetPhysicalDeviceSurfaceCapabilitiesKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR GetPhysicalDeviceSurfaceFormatsKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR GetPhysicalDeviceSurfacePresentModesKHR = nullptr;
    PFN_vkCreateDevice CreateDevice = nullptr;
    PFN_vkDestroyDevice DestroyDevice = nullptr;
    PFN_vkGetDeviceProcAddr GetDeviceProcAddr = nullptr;
    PFN_vkDestroySurfaceKHR DestroySurfaceKHR = nullptr;
    PFN_vkCreateWin32SurfaceKHR CreateWin32SurfaceKHR = nullptr;

    PFN_vkGetDeviceQueue GetDeviceQueue = nullptr;
    PFN_vkDeviceWaitIdle DeviceWaitIdle = nullptr;
    PFN_vkQueueWaitIdle QueueWaitIdle = nullptr;
    PFN_vkCreateSwapchainKHR CreateSwapchainKHR = nullptr;
    PFN_vkDestroySwapchainKHR DestroySwapchainKHR = nullptr;
    PFN_vkGetSwapchainImagesKHR GetSwapchainImagesKHR = nullptr;
    PFN_vkAcquireNextImageKHR AcquireNextImageKHR = nullptr;
    PFN_vkQueueSubmit QueueSubmit = nullptr;
    PFN_vkQueuePresentKHR QueuePresentKHR = nullptr;
    PFN_vkCreateImageView CreateImageView = nullptr;
    PFN_vkDestroyImageView DestroyImageView = nullptr;
    PFN_vkCreateRenderPass CreateRenderPass = nullptr;
    PFN_vkDestroyRenderPass DestroyRenderPass = nullptr;
    PFN_vkCreateFramebuffer CreateFramebuffer = nullptr;
    PFN_vkDestroyFramebuffer DestroyFramebuffer = nullptr;
    PFN_vkCreateCommandPool CreateCommandPool = nullptr;
    PFN_vkDestroyCommandPool DestroyCommandPool = nullptr;
    PFN_vkAllocateCommandBuffers AllocateCommandBuffers = nullptr;
    PFN_vkFreeCommandBuffers FreeCommandBuffers = nullptr;
    PFN_vkBeginCommandBuffer BeginCommandBuffer = nullptr;
    PFN_vkEndCommandBuffer EndCommandBuffer = nullptr;
    PFN_vkCmdBeginRenderPass CmdBeginRenderPass = nullptr;
    PFN_vkCmdEndRenderPass CmdEndRenderPass = nullptr;
    PFN_vkCmdBindPipeline CmdBindPipeline = nullptr;
    PFN_vkCmdBindDescriptorSets CmdBindDescriptorSets = nullptr;
    PFN_vkCmdDraw CmdDraw = nullptr;
    PFN_vkCmdSetViewport CmdSetViewport = nullptr;
    PFN_vkCmdSetScissor CmdSetScissor = nullptr;
    PFN_vkCmdPipelineBarrier CmdPipelineBarrier = nullptr;
    PFN_vkCmdCopyBufferToImage CmdCopyBufferToImage = nullptr;
    PFN_vkCreateBuffer CreateBuffer = nullptr;
    PFN_vkDestroyBuffer DestroyBuffer = nullptr;
    PFN_vkGetBufferMemoryRequirements GetBufferMemoryRequirements = nullptr;
    PFN_vkBindBufferMemory BindBufferMemory = nullptr;
    PFN_vkCreateImage CreateImage = nullptr;
    PFN_vkDestroyImage DestroyImage = nullptr;
    PFN_vkGetImageMemoryRequirements GetImageMemoryRequirements = nullptr;
    PFN_vkBindImageMemory BindImageMemory = nullptr;
    PFN_vkAllocateMemory AllocateMemory = nullptr;
    PFN_vkFreeMemory FreeMemory = nullptr;
    PFN_vkMapMemory MapMemory = nullptr;
    PFN_vkUnmapMemory UnmapMemory = nullptr;
    PFN_vkCreateSampler CreateSampler = nullptr;
    PFN_vkDestroySampler DestroySampler = nullptr;
    PFN_vkCreateShaderModule CreateShaderModule = nullptr;
    PFN_vkDestroyShaderModule DestroyShaderModule = nullptr;
    PFN_vkCreateDescriptorSetLayout CreateDescriptorSetLayout = nullptr;
    PFN_vkDestroyDescriptorSetLayout DestroyDescriptorSetLayout = nullptr;
    PFN_vkCreatePipelineLayout CreatePipelineLayout = nullptr;
    PFN_vkDestroyPipelineLayout DestroyPipelineLayout = nullptr;
    PFN_vkCreateGraphicsPipelines CreateGraphicsPipelines = nullptr;
    PFN_vkDestroyPipeline DestroyPipeline = nullptr;
    PFN_vkCreateDescriptorPool CreateDescriptorPool = nullptr;
    PFN_vkDestroyDescriptorPool DestroyDescriptorPool = nullptr;
    PFN_vkAllocateDescriptorSets AllocateDescriptorSets = nullptr;
    PFN_vkUpdateDescriptorSets UpdateDescriptorSets = nullptr;
    PFN_vkCreateSemaphore CreateSemaphore = nullptr;
    PFN_vkDestroySemaphore DestroySemaphore = nullptr;
    PFN_vkCreateFence CreateFence = nullptr;
    PFN_vkDestroyFence DestroyFence = nullptr;
    PFN_vkWaitForFences WaitForFences = nullptr;
    PFN_vkResetFences ResetFences = nullptr;

    bool loadGlobal() {
        dll = LoadLibraryW(L"vulkan-1.dll");
        if (!dll) return false;
        GetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            reinterpret_cast<void*>(GetProcAddress(dll, "vkGetInstanceProcAddr")));
        if (!GetInstanceProcAddr) return false;

        CreateInstance = reinterpret_cast<PFN_vkCreateInstance>(
            GetInstanceProcAddr(nullptr, "vkCreateInstance"));
        EnumerateInstanceExtensionProperties =
            reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
                GetInstanceProcAddr(nullptr, "vkEnumerateInstanceExtensionProperties"));
        return CreateInstance != nullptr;
    }
};

// The two lists are expanded three times: once to declare the members (above)
// and once per scope to fill them in. Keeping the names in one place is what
// stops the two halves drifting apart.
#define TUX_VK_INSTANCE_FUNCS(F)                                        \
    F(vkDestroyInstance, DestroyInstance)                               \
    F(vkEnumeratePhysicalDevices, EnumeratePhysicalDevices)             \
    F(vkGetPhysicalDeviceProperties, GetPhysicalDeviceProperties)       \
    F(vkGetPhysicalDeviceQueueFamilyProperties, GetPhysicalDeviceQueueFamilyProperties) \
    F(vkGetPhysicalDeviceMemoryProperties, GetPhysicalDeviceMemoryProperties) \
    F(vkGetPhysicalDeviceSurfaceSupportKHR, GetPhysicalDeviceSurfaceSupportKHR) \
    F(vkGetPhysicalDeviceSurfaceCapabilitiesKHR, GetPhysicalDeviceSurfaceCapabilitiesKHR) \
    F(vkGetPhysicalDeviceSurfaceFormatsKHR, GetPhysicalDeviceSurfaceFormatsKHR) \
    F(vkGetPhysicalDeviceSurfacePresentModesKHR, GetPhysicalDeviceSurfacePresentModesKHR) \
    F(vkCreateDevice, CreateDevice)                                     \
    F(vkGetDeviceProcAddr, GetDeviceProcAddr)                           \
    F(vkDestroySurfaceKHR, DestroySurfaceKHR)                           \
    F(vkCreateWin32SurfaceKHR, CreateWin32SurfaceKHR)

#define TUX_VK_DEVICE_FUNCS(F)                                          \
    F(vkDestroyDevice, DestroyDevice)                                   \
    F(vkGetDeviceQueue, GetDeviceQueue)                                 \
    F(vkDeviceWaitIdle, DeviceWaitIdle)                                 \
    F(vkQueueWaitIdle, QueueWaitIdle)                                   \
    F(vkCreateSwapchainKHR, CreateSwapchainKHR)                         \
    F(vkDestroySwapchainKHR, DestroySwapchainKHR)                       \
    F(vkGetSwapchainImagesKHR, GetSwapchainImagesKHR)                   \
    F(vkAcquireNextImageKHR, AcquireNextImageKHR)                       \
    F(vkQueueSubmit, QueueSubmit)                                       \
    F(vkQueuePresentKHR, QueuePresentKHR)                               \
    F(vkCreateImageView, CreateImageView)                               \
    F(vkDestroyImageView, DestroyImageView)                             \
    F(vkCreateRenderPass, CreateRenderPass)                             \
    F(vkDestroyRenderPass, DestroyRenderPass)                           \
    F(vkCreateFramebuffer, CreateFramebuffer)                           \
    F(vkDestroyFramebuffer, DestroyFramebuffer)                         \
    F(vkCreateCommandPool, CreateCommandPool)                           \
    F(vkDestroyCommandPool, DestroyCommandPool)                         \
    F(vkAllocateCommandBuffers, AllocateCommandBuffers)                 \
    F(vkFreeCommandBuffers, FreeCommandBuffers)                         \
    F(vkBeginCommandBuffer, BeginCommandBuffer)                         \
    F(vkEndCommandBuffer, EndCommandBuffer)                             \
    F(vkCmdBeginRenderPass, CmdBeginRenderPass)                         \
    F(vkCmdEndRenderPass, CmdEndRenderPass)                             \
    F(vkCmdBindPipeline, CmdBindPipeline)                               \
    F(vkCmdBindDescriptorSets, CmdBindDescriptorSets)                   \
    F(vkCmdDraw, CmdDraw)                                               \
    F(vkCmdSetViewport, CmdSetViewport)                                 \
    F(vkCmdSetScissor, CmdSetScissor)                                   \
    F(vkCmdPipelineBarrier, CmdPipelineBarrier)                         \
    F(vkCmdCopyBufferToImage, CmdCopyBufferToImage)                     \
    F(vkCreateBuffer, CreateBuffer)                                     \
    F(vkDestroyBuffer, DestroyBuffer)                                   \
    F(vkGetBufferMemoryRequirements, GetBufferMemoryRequirements)       \
    F(vkBindBufferMemory, BindBufferMemory)                             \
    F(vkCreateImage, CreateImage)                                       \
    F(vkDestroyImage, DestroyImage)                                     \
    F(vkGetImageMemoryRequirements, GetImageMemoryRequirements)         \
    F(vkBindImageMemory, BindImageMemory)                               \
    F(vkAllocateMemory, AllocateMemory)                                 \
    F(vkFreeMemory, FreeMemory)                                         \
    F(vkMapMemory, MapMemory)                                           \
    F(vkUnmapMemory, UnmapMemory)                                       \
    F(vkCreateSampler, CreateSampler)                                   \
    F(vkDestroySampler, DestroySampler)                                 \
    F(vkCreateShaderModule, CreateShaderModule)                         \
    F(vkDestroyShaderModule, DestroyShaderModule)                       \
    F(vkCreateDescriptorSetLayout, CreateDescriptorSetLayout)           \
    F(vkDestroyDescriptorSetLayout, DestroyDescriptorSetLayout)         \
    F(vkCreatePipelineLayout, CreatePipelineLayout)                     \
    F(vkDestroyPipelineLayout, DestroyPipelineLayout)                   \
    F(vkCreateGraphicsPipelines, CreateGraphicsPipelines)               \
    F(vkDestroyPipeline, DestroyPipeline)                               \
    F(vkCreateDescriptorPool, CreateDescriptorPool)                     \
    F(vkDestroyDescriptorPool, DestroyDescriptorPool)                   \
    F(vkAllocateDescriptorSets, AllocateDescriptorSets)                 \
    F(vkUpdateDescriptorSets, UpdateDescriptorSets)                     \
    F(vkCreateSemaphore, CreateSemaphore)                               \
    F(vkDestroySemaphore, DestroySemaphore)                             \
    F(vkCreateFence, CreateFence)                                       \
    F(vkDestroyFence, DestroyFence)                                     \
    F(vkWaitForFences, WaitForFences)                                   \
    F(vkResetFences, ResetFences)

bool loadVkInstance(VkApi& vk, VkInstance instance) {
#define TUX_VK_FETCH_I(name, member)                                           \
    vk.member = reinterpret_cast<decltype(vk.member)>(vk.GetInstanceProcAddr(instance, #name));
    TUX_VK_INSTANCE_FUNCS(TUX_VK_FETCH_I)
#undef TUX_VK_FETCH_I
    return vk.CreateWin32SurfaceKHR != nullptr && vk.CreateDevice != nullptr &&
           vk.EnumeratePhysicalDevices != nullptr;
}

bool loadVkDevice(VkApi& vk, VkDevice device) {
#define TUX_VK_FETCH_D(name, member)                                           \
    vk.member = reinterpret_cast<decltype(vk.member)>(vk.GetDeviceProcAddr(device, #name));
    TUX_VK_DEVICE_FUNCS(TUX_VK_FETCH_D)
#undef TUX_VK_FETCH_D
    return vk.CreateSwapchainKHR != nullptr && vk.QueuePresentKHR != nullptr &&
           vk.CmdDraw != nullptr && vk.CreateGraphicsPipelines != nullptr;
}

// --- the uniform block ----------------------------------------------------
//
// Matches the vertex shader's std140 block exactly:
//
//   layout(std140, binding = 0) uniform buf {
//       mat4 MVP; vec4 position[36]; vec4 attr[36];
//   } ubuf;
//
// The shader indexes position[]/attr[] by gl_VertexIndex, so there is no vertex
// buffer anywhere: a frame is a single vkCmdDraw(cmd, 36, 1, 0, 0). std140
// pads each vec4 to 16 bytes, which float[4] already is, so the layout below
// can be copied straight into the mapped buffer.

constexpr int kVertexCount = 36;

struct Uniforms {
    float mvp[16];
    float position[kVertexCount][4];
    float attr[kVertexCount][4];
};

// vkcube's cube, taken verbatim from Vulkan-Tools/cube/cube.cpp: the winding
// and the per-face UVs are what make the LunarG texture land on the six faces
// the way vkcube shows it.
#include "vk_cube.mesh.inc"

void buildCube(Uniforms& u) {
    constexpr int kFloatsPerVertex = 3;
    for (int i = 0; i < kVertexCount; ++i) {
        u.position[i][0] = kCubeVertex[i * kFloatsPerVertex + 0];
        u.position[i][1] = kCubeVertex[i * kFloatsPerVertex + 1];
        u.position[i][2] = kCubeVertex[i * kFloatsPerVertex + 2];
        u.position[i][3] = 1.0f;
        u.attr[i][0] = kCubeUv[i * 2 + 0];
        u.attr[i][1] = kCubeUv[i * 2 + 1];
        u.attr[i][2] = 0.0f;
        u.attr[i][3] = 0.0f;
    }
}

// vkcube's texture, taken verbatim from Vulkan-Tools/cube/lunarg.ppm.h
// (Apache-2.0). It is a P6 PPM compiled in as a byte array, exactly as
// upstream does, so nothing decodes an image at run time.
#include "vk_cube.texture.inc"

Texture makeCubeTexture() {
    Texture tex;

    // The array starts with the PPM header "P6\n256 256\n255\n", after which
    // the pixel data is packed RGB triples. Parsing the header here rather
    // than hardcoding the size keeps the array authoritative.
    size_t offset = 0;
    if (std::strncmp(reinterpret_cast<const char*>(lunargPpm), "P6", 2) != 0) {
        return tex;
    }
    offset = 2;
    const auto readNumber = [&](int& out) {
        while (offset < sizeof(lunargPpm) &&
               (lunargPpm[offset] == ' ' || lunargPpm[offset] == '\n' ||
                lunargPpm[offset] == '\r' || lunargPpm[offset] == '\t')) {
            ++offset;
        }
        int value = 0;
        bool any = false;
        while (offset < sizeof(lunargPpm) && lunargPpm[offset] >= '0' &&
               lunargPpm[offset] <= '9') {
            value = value * 10 + (lunargPpm[offset] - '0');
            ++offset;
            any = true;
        }
        out = value;
        return any;
    };

    int width = 0, height = 0, maxValue = 0;
    if (!readNumber(width) || !readNumber(height) || !readNumber(maxValue)) {
        return tex;
    }
    if (width <= 0 || height <= 0 || maxValue != 255) return tex;
    if (offset >= sizeof(lunargPpm) || lunargPpm[offset] != '\n') return tex;
    ++offset;

    const size_t bytes = static_cast<size_t>(width) * height * 3;
    if (offset + bytes > sizeof(lunargPpm)) return tex;

    tex.width = width;
    tex.height = height;
    tex.pixels.resize(static_cast<size_t>(width) * height * 4);
    for (int i = 0; i < width * height; ++i) {
        tex.pixels[static_cast<size_t>(i) * 4 + 0] = lunargPpm[offset + i * 3 + 0];
        tex.pixels[static_cast<size_t>(i) * 4 + 1] = lunargPpm[offset + i * 3 + 1];
        tex.pixels[static_cast<size_t>(i) * 4 + 2] = lunargPpm[offset + i * 3 + 2];
        tex.pixels[static_cast<size_t>(i) * 4 + 3] = 255;
    }
    return tex;
}

// vkcube clears to a mid grey and the depth buffer to 1.0.
constexpr float kClearGrey = 0.2f;

}  // namespace

class VulkanTool : public Tool {
public:
    bool init(HWND host) override;
    void resize(int width, int height) override;
    void render(double elapsed) override;
    void shutdown() override;
    std::wstring deviceInfo() const override { return deviceInfo_; }

private:
    bool createInstance();
    bool createSurface();
    bool pickDevice();
    bool createDevice();
    bool createCommandPool();
    bool createRenderPass();
    bool createDepthResources();
    void destroyDepthResources();
    bool createDescriptors();
    bool createPipeline();
    bool createResources();
    bool createSwapchain();
    bool createSyncObjects();
    void destroySwapchain();
    bool recreateSwapchain();
    void updateUniforms(double elapsed);
    void recordFrame(uint32_t index);
    uint32_t memoryType(uint32_t bits, VkMemoryPropertyFlags want) const;

    VkApi vk_;
    HWND host_ = nullptr;
    int width_ = 1;
    int height_ = 1;
    std::wstring deviceInfo_;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;
    VkPhysicalDeviceMemoryProperties memProps_ = {};

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D extent_ = {1, 1};
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;
    std::vector<VkFramebuffer> framebuffers_;

    // One depth image shared by every swapchain image, as upstream does: it is
    // re-cleared at the start of each frame, so they never overlap in use.
    VkImage depthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory_ = VK_NULL_HANDLE;
    VkImageView depthView_ = VK_NULL_HANDLE;
    VkFormat depthFormat_ = VK_FORMAT_D16_UNORM;

    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;

    VkBuffer uniformBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory uniformMemory_ = VK_NULL_HANDLE;
    void* uniformMapped_ = nullptr;

    VkImage texture_ = VK_NULL_HANDLE;
    VkDeviceMemory textureMemory_ = VK_NULL_HANDLE;
    VkImageView textureView_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;

    VkSemaphore imageAvailable_ = VK_NULL_HANDLE;
    VkSemaphore renderFinished_ = VK_NULL_HANDLE;
    VkFence inFlight_ = VK_NULL_HANDLE;
};

uint32_t VulkanTool::memoryType(uint32_t bits, VkMemoryPropertyFlags want) const {
    for (uint32_t i = 0; i < memProps_.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) && (memProps_.memoryTypes[i].propertyFlags & want) == want) {
            return i;
        }
    }
    return 0xFFFFFFFFu;
}

bool VulkanTool::createInstance() {
    uint32_t count = 0;
    vk_.EnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> have(count);
    if (count) {
        vk_.EnumerateInstanceExtensionProperties(nullptr, &count, have.data());
    }
    const auto supported = [&have](const char* name) {
        for (const VkExtensionProperties& e : have) {
            if (std::strcmp(e.extensionName, name) == 0) return true;
        }
        return false;
    };

    if (!supported(VK_KHR_SURFACE_EXTENSION_NAME) ||
        !supported(VK_KHR_WIN32_SURFACE_EXTENSION_NAME)) {
        return false;
    }

    const char* extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME,
                                VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    VkApplicationInfo app = {};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "Tux-Tools";
    app.pEngineName = "Tux-Tools";
    app.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = 2;
    ci.ppEnabledExtensionNames = extensions;
    return ok(vk_.CreateInstance(&ci, nullptr, &instance_));
}

bool VulkanTool::createSurface() {
    VkWin32SurfaceCreateInfoKHR ci = {};
    ci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    ci.hinstance = GetModuleHandleW(nullptr);
    ci.hwnd = host_;
    return ok(vk_.CreateWin32SurfaceKHR(instance_, &ci, nullptr, &surface_));
}

bool VulkanTool::pickDevice() {
    uint32_t count = 0;
    if (!ok(vk_.EnumeratePhysicalDevices(instance_, &count, nullptr)) || count == 0) {
        return false;
    }
    std::vector<VkPhysicalDevice> devices(count);
    vk_.EnumeratePhysicalDevices(instance_, &count, devices.data());

    for (VkPhysicalDevice dev : devices) {
        uint32_t families = 0;
        vk_.GetPhysicalDeviceQueueFamilyProperties(dev, &families, nullptr);
        std::vector<VkQueueFamilyProperties> props(families);
        vk_.GetPhysicalDeviceQueueFamilyProperties(dev, &families, props.data());

        for (uint32_t i = 0; i < families; ++i) {
            if (!(props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
            VkBool32 present = VK_FALSE;
            if (!ok(vk_.GetPhysicalDeviceSurfaceSupportKHR(dev, i, surface_, &present))) {
                continue;
            }
            if (!present) continue;

            physical_ = dev;
            queueFamily_ = i;
            return true;
        }
    }
    return false;
}

bool VulkanTool::createDevice() {
    vk_.GetPhysicalDeviceMemoryProperties(physical_, &memProps_);

    VkPhysicalDeviceProperties props = {};
    vk_.GetPhysicalDeviceProperties(physical_, &props);
    std::wstring name(props.deviceName, props.deviceName + std::strlen(props.deviceName));
    deviceInfo_ = L"Vulkan - " + name;

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo q = {};
    q.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    q.queueFamilyIndex = queueFamily_;
    q.queueCount = 1;
    q.pQueuePriorities = &priority;

    const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount = 1;
    ci.pQueueCreateInfos = &q;
    ci.enabledExtensionCount = 1;
    ci.ppEnabledExtensionNames = extensions;
    if (!ok(vk_.CreateDevice(physical_, &ci, nullptr, &device_))) return false;
    if (!loadVkDevice(vk_, device_)) return false;

    vk_.GetDeviceQueue(device_, queueFamily_, 0, &queue_);
    return true;
}

bool VulkanTool::createCommandPool() {
    VkCommandPoolCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ci.queueFamilyIndex = queueFamily_;
    return ok(vk_.CreateCommandPool(device_, &ci, nullptr, &commandPool_));
}

bool VulkanTool::createRenderPass() {
    VkAttachmentDescription colour = {};
    colour.format = format_;
    colour.samples = VK_SAMPLE_COUNT_1_BIT;
    colour.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colour.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colour.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colour.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colour.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colour.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // Second attachment: the depth buffer vkcube uses, D16_UNORM.
    VkAttachmentDescription depth = {};
    depth.format = depthFormat_;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    const VkAttachmentDescription attachments[2] = {colour, depth};

    VkAttachmentReference ref = {};
    ref.attachment = 0;
    ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef = {};
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription sub = {};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    sub.pDepthStencilAttachment = &depthRef;

    // Without a dependency the layout transitions can be reordered past the
    // draws that need them, which shows up as a black or flickering first
    // frame. Two of them: one for the colour attachment, and one for the depth
    // buffer, which is written and read by every frame in turn.
    VkSubpassDependency deps[2] = {};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].dstSubpass = 0;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[1].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 2;
    ci.pAttachments = attachments;
    ci.subpassCount = 1;
    ci.pSubpasses = &sub;
    ci.dependencyCount = 2;
    ci.pDependencies = deps;
    return ok(vk_.CreateRenderPass(device_, &ci, nullptr, &renderPass_));
}

bool VulkanTool::createDepthResources() {
    VkImageCreateInfo ii = {};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = depthFormat_;
    ii.extent.width = extent_.width;
    ii.extent.height = extent_.height;
    ii.extent.depth = 1;
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!ok(vk_.CreateImage(device_, &ii, nullptr, &depthImage_))) return false;

    VkMemoryRequirements req = {};
    vk_.GetImageMemoryRequirements(device_, depthImage_, &req);
    VkMemoryAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (ai.memoryTypeIndex == 0xFFFFFFFFu) return false;
    if (!ok(vk_.AllocateMemory(device_, &ai, nullptr, &depthMemory_))) return false;
    if (!ok(vk_.BindImageMemory(device_, depthImage_, depthMemory_, 0))) return false;

    VkImageViewCreateInfo vi = {};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = depthImage_;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = depthFormat_;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    return ok(vk_.CreateImageView(device_, &vi, nullptr, &depthView_));
}

void VulkanTool::destroyDepthResources() {
    if (!device_) return;
    if (depthView_) { vk_.DestroyImageView(device_, depthView_, nullptr); depthView_ = VK_NULL_HANDLE; }
    if (depthImage_) { vk_.DestroyImage(device_, depthImage_, nullptr); depthImage_ = VK_NULL_HANDLE; }
    if (depthMemory_) { vk_.FreeMemory(device_, depthMemory_, nullptr); depthMemory_ = VK_NULL_HANDLE; }
}

bool VulkanTool::createDescriptors() {
    VkDescriptorSetLayoutBinding b[2] = {};
    b[0].binding = 0;
    b[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    b[0].descriptorCount = 1;
    b[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    b[1].binding = 1;
    b[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b[1].descriptorCount = 1;
    b[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo li = {};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 2;
    li.pBindings = b;
    if (!ok(vk_.CreateDescriptorSetLayout(device_, &li, nullptr, &setLayout_))) return false;

    VkDescriptorPoolSize sizes[2] = {};
    sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    sizes[0].descriptorCount = 1;
    sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo pi = {};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets = 1;
    pi.poolSizeCount = 2;
    pi.pPoolSizes = sizes;
    if (!ok(vk_.CreateDescriptorPool(device_, &pi, nullptr, &pool_))) return false;

    VkDescriptorSetAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = pool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &setLayout_;
    return ok(vk_.AllocateDescriptorSets(device_, &ai, &set_));
}

bool VulkanTool::createPipeline() {
    // The SPIR-V travel with the source as generated uint32 arrays, so no
    // shader compiler is needed at build time. They are vkcube's own modules,
    // used unmodified (Apache-2.0).
    static const uint32_t kVert[] = {
#include "vk_cube.vert.inc"
    };
    static const uint32_t kFrag[] = {
#include "vk_cube.frag.inc"
    };

    VkShaderModuleCreateInfo vi = {};
    vi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vi.codeSize = sizeof(kVert);
    vi.pCode = kVert;
    VkShaderModule vs = VK_NULL_HANDLE;
    if (!ok(vk_.CreateShaderModule(device_, &vi, nullptr, &vs))) return false;

    VkShaderModuleCreateInfo fi = {};
    fi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fi.codeSize = sizeof(kFrag);
    fi.pCode = kFrag;
    VkShaderModule fs = VK_NULL_HANDLE;
    if (!ok(vk_.CreateShaderModule(device_, &fi, nullptr, &fs))) {
        vk_.DestroyShaderModule(device_, vs, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";

    // No vertex input at all: the shader reads its vertices out of the uniform
    // block, indexed by gl_VertexIndex.
    VkPipelineVertexInputStateCreateInfo vin = {};
    vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo ia = {};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp = {};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs = {};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    // vkcube's own rasterisation state: back faces culled, counter-clockwise
    // front faces, with depth testing and writing enabled. The depth buffer is
    // a real attachment (see createRenderPass), not a trick to avoid one.
    rs.cullMode = VK_CULL_MODE_BACK_BIT;
    // The vertex table winds counter-clockwise seen from outside the cube, and
    // vkcube asks for counter-clockwise front faces. The two flips applied to
    // the projection above (Y for Vulkan's clip space, X for the handedness
    // difference) mirror the geometry an even number of times, so the winding
    // arrives on screen unchanged.
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.depthClampEnable = VK_FALSE;
    rs.depthBiasEnable = VK_FALSE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms = {};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // vkcube's depth state: test and write enabled, less-or-equal comparison.
    VkPipelineDepthStencilStateCreateInfo dsState = {};
    dsState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dsState.depthTestEnable = VK_TRUE;
    dsState.depthWriteEnable = VK_TRUE;
    dsState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    dsState.depthBoundsTestEnable = VK_FALSE;
    dsState.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blend = {};
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo cb = {};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &blend;

    const VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds = {};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dyn;

    VkPipelineLayoutCreateInfo pl = {};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &setLayout_;
    if (!ok(vk_.CreatePipelineLayout(device_, &pl, nullptr, &pipelineLayout_))) {
        vk_.DestroyShaderModule(device_, fs, nullptr);
        vk_.DestroyShaderModule(device_, vs, nullptr);
        return false;
    }

    VkGraphicsPipelineCreateInfo gp = {};
    gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp.stageCount = 2;
    gp.pStages = stages;
    gp.pVertexInputState = &vin;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pDepthStencilState = &dsState;
    gp.pColorBlendState = &cb;
    gp.pDynamicState = &ds;
    gp.layout = pipelineLayout_;
    gp.renderPass = renderPass_;
    gp.subpass = 0;

    const VkResult r =
        vk_.CreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &pipeline_);
    vk_.DestroyShaderModule(device_, fs, nullptr);
    vk_.DestroyShaderModule(device_, vs, nullptr);
    return ok(r);
}

bool VulkanTool::createResources() {
    // --- uniform buffer, persistently mapped ---
    VkBufferCreateInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = sizeof(Uniforms);
    bi.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!ok(vk_.CreateBuffer(device_, &bi, nullptr, &uniformBuffer_))) return false;

    VkMemoryRequirements req = {};
    vk_.GetBufferMemoryRequirements(device_, uniformBuffer_, &req);
    VkMemoryAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memoryType(req.memoryTypeBits,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (ai.memoryTypeIndex == 0xFFFFFFFFu) return false;
    if (!ok(vk_.AllocateMemory(device_, &ai, nullptr, &uniformMemory_))) return false;
    if (!ok(vk_.BindBufferMemory(device_, uniformBuffer_, uniformMemory_, 0))) return false;
    if (!ok(vk_.MapMemory(device_, uniformMemory_, 0, sizeof(Uniforms), 0, &uniformMapped_))) {
        return false;
    }

    Uniforms u = {};
    buildCube(u);
    std::memcpy(uniformMapped_, &u, sizeof(u));

    // --- texture, through a staging buffer and one-shot commands ---
    const Texture source = makeCubeTexture();

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    {
        VkBufferCreateInfo sbi = {};
        sbi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        sbi.size = source.pixels.size();
        sbi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        sbi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (!ok(vk_.CreateBuffer(device_, &sbi, nullptr, &staging))) return false;

        VkMemoryRequirements sreq = {};
        vk_.GetBufferMemoryRequirements(device_, staging, &sreq);
        VkMemoryAllocateInfo sai = {};
        sai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        sai.allocationSize = sreq.size;
        sai.memoryTypeIndex = memoryType(sreq.memoryTypeBits,
                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (sai.memoryTypeIndex == 0xFFFFFFFFu) return false;
        if (!ok(vk_.AllocateMemory(device_, &sai, nullptr, &stagingMemory))) return false;
        if (!ok(vk_.BindBufferMemory(device_, staging, stagingMemory, 0))) return false;

        void* dst = nullptr;
        if (!ok(vk_.MapMemory(device_, stagingMemory, 0, sbi.size, 0, &dst))) return false;
        std::memcpy(dst, source.pixels.data(), source.pixels.size());
        vk_.UnmapMemory(device_, stagingMemory);
    }

    VkImageCreateInfo ii = {};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_R8G8B8A8_UNORM;
    ii.extent.width = static_cast<uint32_t>(source.width);
    ii.extent.height = static_cast<uint32_t>(source.height);
    ii.extent.depth = 1;
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!ok(vk_.CreateImage(device_, &ii, nullptr, &texture_))) return false;

    VkMemoryRequirements ireq = {};
    vk_.GetImageMemoryRequirements(device_, texture_, &ireq);
    VkMemoryAllocateInfo iai = {};
    iai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    iai.allocationSize = ireq.size;
    iai.memoryTypeIndex = memoryType(ireq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (iai.memoryTypeIndex == 0xFFFFFFFFu) return false;
    if (!ok(vk_.AllocateMemory(device_, &iai, nullptr, &textureMemory_))) return false;
    if (!ok(vk_.BindImageMemory(device_, texture_, textureMemory_, 0))) return false;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo cai = {};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = commandPool_;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    if (!ok(vk_.AllocateCommandBuffers(device_, &cai, &cmd))) return false;

    VkCommandBufferBeginInfo cbi = {};
    cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vk_.BeginCommandBuffer(cmd, &cbi);

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vk_.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                           &barrier);

    VkBufferImageCopy region = {};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = static_cast<uint32_t>(source.width);
    region.imageExtent.height = static_cast<uint32_t>(source.height);
    region.imageExtent.depth = 1;
    vk_.CmdCopyBufferToImage(cmd, staging, texture_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                             &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vk_.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                           &barrier);

    vk_.EndCommandBuffer(cmd);

    VkSubmitInfo submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    if (!ok(vk_.QueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE))) return false;
    vk_.QueueWaitIdle(queue_);

    vk_.FreeCommandBuffers(device_, commandPool_, 1, &cmd);
    vk_.DestroyBuffer(device_, staging, nullptr);
    vk_.FreeMemory(device_, stagingMemory, nullptr);

    VkImageViewCreateInfo tvi = {};
    tvi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    tvi.image = texture_;
    tvi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    tvi.format = VK_FORMAT_R8G8B8A8_UNORM;
    tvi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    tvi.subresourceRange.levelCount = 1;
    tvi.subresourceRange.layerCount = 1;
    if (!ok(vk_.CreateImageView(device_, &tvi, nullptr, &textureView_))) return false;

    VkSamplerCreateInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxLod = 0.0f;
    if (!ok(vk_.CreateSampler(device_, &si, nullptr, &sampler_))) return false;

    VkDescriptorBufferInfo buf = {};
    buf.buffer = uniformBuffer_;
    buf.offset = 0;
    buf.range = sizeof(Uniforms);

    VkDescriptorImageInfo img = {};
    img.sampler = sampler_;
    img.imageView = textureView_;
    img.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet writes[2] = {};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = set_;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &buf;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = set_;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = &img;
    vk_.UpdateDescriptorSets(device_, 2, writes, 0, nullptr);
    return true;
}

bool VulkanTool::createSwapchain() {
    VkSurfaceCapabilitiesKHR caps = {};
    if (!ok(vk_.GetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &caps))) {
        return false;
    }

    uint32_t formatCount = 0;
    vk_.GetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &formatCount, nullptr);
    if (formatCount == 0) return false;
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vk_.GetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &formatCount, formats.data());

    VkSurfaceFormatKHR chosen = formats[0];
    for (const VkSurfaceFormatKHR& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = f;
            break;
        }
    }
    format_ = chosen.format;

    // For a frame-rate test the present mode matters: FIFO is vsync-limited.
    // Prefer IMMEDIATE, then MAILBOX, and fall back to FIFO only if neither is
    // offered - the same "let the device run flat out" rule the Direct3D
    // backends follow with their immediate presentation intervals.
    uint32_t modeCount = 0;
    vk_.GetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &modeCount, nullptr);
    std::vector<VkPresentModeKHR> modes(modeCount);
    if (modeCount) {
        vk_.GetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &modeCount, modes.data());
    }
    VkPresentModeKHR present = VK_PRESENT_MODE_FIFO_KHR;
    for (VkPresentModeKHR m : modes) {
        if (m == VK_PRESENT_MODE_IMMEDIATE_KHR) { present = m; break; }
    }
    if (present == VK_PRESENT_MODE_FIFO_KHR) {
        for (VkPresentModeKHR m : modes) {
            if (m == VK_PRESENT_MODE_MAILBOX_KHR) { present = m; break; }
        }
    }

    // currentExtent is 0xFFFFFFFF when the surface lets the application choose.
    if (caps.currentExtent.width != 0xFFFFFFFFu) {
        extent_ = caps.currentExtent;
    } else {
        extent_.width = static_cast<uint32_t>(width_);
        extent_.height = static_cast<uint32_t>(height_);
        if (extent_.width < caps.minImageExtent.width) extent_.width = caps.minImageExtent.width;
        if (extent_.width > caps.maxImageExtent.width) extent_.width = caps.maxImageExtent.width;
        if (extent_.height < caps.minImageExtent.height) extent_.height = caps.minImageExtent.height;
        if (extent_.height > caps.maxImageExtent.height) extent_.height = caps.maxImageExtent.height;
    }
    if (extent_.width == 0 || extent_.height == 0) return false;

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) {
        imageCount = caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR ci = {};
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = surface_;
    ci.minImageCount = imageCount;
    ci.imageFormat = format_;
    ci.imageColorSpace = chosen.colorSpace;
    ci.imageExtent = extent_;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = present;
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = VK_NULL_HANDLE;

    if (!ok(vk_.CreateSwapchainKHR(device_, &ci, nullptr, &swapchain_))) return false;

    uint32_t count = 0;
    vk_.GetSwapchainImagesKHR(device_, swapchain_, &count, nullptr);
    images_.resize(count);
    vk_.GetSwapchainImagesKHR(device_, swapchain_, &count, images_.data());

    views_.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        VkImageViewCreateInfo vi = {};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = images_[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = format_;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        if (!ok(vk_.CreateImageView(device_, &vi, nullptr, &views_[i]))) return false;
    }

    // The depth image must exist before the framebuffers reference it, and it is
    // sized from the swapchain extent, so it is created here.
    if (!createDepthResources()) return false;

    framebuffers_.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        VkFramebufferCreateInfo fi = {};
        fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        const VkImageView fbAttachments[2] = {views_[i], depthView_};
        fi.renderPass = renderPass_;
        fi.attachmentCount = 2;
        fi.pAttachments = fbAttachments;
        fi.width = extent_.width;
        fi.height = extent_.height;
        fi.layers = 1;
        if (!ok(vk_.CreateFramebuffer(device_, &fi, nullptr, &framebuffers_[i]))) return false;
    }

    commandBuffers_.resize(count);
    VkCommandBufferAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = commandPool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = count;
    return ok(vk_.AllocateCommandBuffers(device_, &ai, commandBuffers_.data()));
}

void VulkanTool::destroySwapchain() {
    if (!device_) return;
    if (!commandBuffers_.empty() && commandPool_) {
        vk_.FreeCommandBuffers(device_, commandPool_,
                               static_cast<uint32_t>(commandBuffers_.size()),
                               commandBuffers_.data());
        commandBuffers_.clear();
    }
    for (VkFramebuffer fb : framebuffers_) {
        if (fb) vk_.DestroyFramebuffer(device_, fb, nullptr);
    }
    framebuffers_.clear();
    for (VkImageView v : views_) {
        if (v) vk_.DestroyImageView(device_, v, nullptr);
    }
    views_.clear();
    images_.clear();
    // The framebuffers above were the last users of the depth view.
    destroyDepthResources();
    if (swapchain_) {
        vk_.DestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

bool VulkanTool::recreateSwapchain() {
    if (!device_) return false;
    vk_.DeviceWaitIdle(device_);
    destroySwapchain();
    // createSwapchain rebuilds the depth image as well, since it is sized to
    // the new extent.
    return createSwapchain();
}

bool VulkanTool::createSyncObjects() {
    VkSemaphoreCreateInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fi = {};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    // Signalled, so the first frame's wait returns immediately instead of
    // blocking forever on a fence that has never been submitted.
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    if (!ok(vk_.CreateSemaphore(device_, &si, nullptr, &imageAvailable_))) return false;
    if (!ok(vk_.CreateSemaphore(device_, &si, nullptr, &renderFinished_))) return false;
    return ok(vk_.CreateFence(device_, &fi, nullptr, &inFlight_));
}

bool VulkanTool::init(HWND host) {
    RECT rc;
    GetClientRect(host, &rc);
    width_ = rc.right - rc.left;
    height_ = rc.bottom - rc.top;
    if (width_ <= 0) width_ = 1;
    if (height_ <= 0) height_ = 1;
    host_ = host;

    if (!vk_.loadGlobal()) return false;
    if (!createInstance()) return false;
    if (!loadVkInstance(vk_, instance_)) return false;
    if (!createSurface()) return false;
    if (!pickDevice()) return false;
    if (!createDevice()) return false;
    if (!createCommandPool()) return false;
    if (!createRenderPass()) return false;
    if (!createDescriptors()) return false;
    if (!createPipeline()) return false;
    if (!createResources()) return false;
    if (!createSwapchain()) return false;
    if (!createSyncObjects()) return false;
    return true;
}

void VulkanTool::resize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (width == width_ && height == height_) return;
    width_ = width;
    height_ = height;
    recreateSwapchain();
}

void VulkanTool::updateUniforms(double elapsed) {
    if (!uniformMapped_) return;
    const float t = static_cast<float>(elapsed);

    // Camera and lens are vkcube's: the eye sits at (0, 3, 5) looking at the
    // origin, with a 45 degree vertical field of view and a 1:1 aspect -
    // vkcube always renders into the largest centred square the surface
    // allows, letterboxed inside whatever shape the window happens to be.
    const Mat4 model = rotationY(t * 0.7f);
    const Mat4 view = lookAtLH(0.0f, 3.0f, 5.0f, 0.0f, 0.0f, 0.0f);
    Mat4 proj = perspectiveFovLH(0.7853981f, 1.0f, 0.1f, 100.0f);

    // Two sign flips, both about Vulkan's clip space and handedness:
    //
    //   m[5]  Vulkan's clip space puts +Y downwards, where the helper builds
    //         the Direct3D/OpenGL convention of +Y up. vkcube has the same
    //         line, `projection_matrix[1][1] *= -1`. Flipping the viewport
    //         height instead looks equivalent but is not: this scene's
    //         fragment shader derives its normal from dFdx/dFdy, and a
    //         negative viewport inverts dFdy too, so every face came out
    //         facing away from the light and the cube rendered black.
    //
    //   m[0]  vkcube's camera is right-handed; this project's lookAtLH is
    //         left-handed, so its X basis comes out negated. The cube's shape
    //         is symmetric and so hides it, but the LunarG texture read
    //         backwards. Negating the projection's X undoes it.
    proj.m[5] = -proj.m[5];
    proj.m[0] = -proj.m[0];

    const Mat4 mvp = multiply(multiply(model, view), proj);

    // The helpers build row-major, row-vector matrices. Laid out linearly that
    // happens to equal the column-major form GLSL reads for mat4 * vec4, so a
    // plain byte copy is correct - the Direct3D backends rely on the same
    // property for their row_major constant buffers.
    std::memcpy(uniformMapped_, mvp.m, sizeof(mvp.m));
}

void VulkanTool::recordFrame(uint32_t index) {
    VkCommandBuffer cmd = commandBuffers_[index];

    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vk_.BeginCommandBuffer(cmd, &bi);

    // vkcube clears to a mid grey and the depth buffer to 1.0. The grey also
    // fills the letterbox around the square viewport.
    VkClearValue clear[2] = {};
    clear[0].color.float32[0] = kClearGrey;
    clear[0].color.float32[1] = kClearGrey;
    clear[0].color.float32[2] = kClearGrey;
    clear[0].color.float32[3] = 1.0f;
    clear[1].depthStencil.depth = 1.0f;
    clear[1].depthStencil.stencil = 0;

    VkRenderPassBeginInfo rp = {};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = renderPass_;
    rp.framebuffer = framebuffers_[index];
    rp.renderArea.extent = extent_;
    rp.clearValueCount = 2;
    rp.pClearValues = clear;
    vk_.CmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    vk_.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vk_.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &set_,
                              0, nullptr);

    // An ordinary positive viewport: the Y flip that Vulkan needs lives in the
    // projection matrix instead, so that the fragment shader's dFdy keeps its
    // usual sign and the normal comes out facing the camera.
    // vkcube renders into the largest centred square the surface allows, so
    // the cube keeps its proportions whatever the window shape. The clear
    // colour fills the rest.
    const float w = static_cast<float>(extent_.width);
    const float h = static_cast<float>(extent_.height);
    const float dimension = w < h ? w : h;
    VkViewport viewport = {};
    viewport.x = w < h ? 0.0f : (w - h) / 2.0f;
    viewport.y = w < h ? (h - w) / 2.0f : 0.0f;
    viewport.width = dimension;
    viewport.height = dimension;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vk_.CmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.extent = extent_;
    vk_.CmdSetScissor(cmd, 0, 1, &scissor);

    // No vertex buffer at all: the shader reads position[]/attr[] out of the
    // uniform block, indexed by gl_VertexIndex.
    vk_.CmdDraw(cmd, kVertexCount, 1, 0, 0);

    vk_.CmdEndRenderPass(cmd);
    vk_.EndCommandBuffer(cmd);
}
void VulkanTool::render(double elapsed) {
    if (!device_ || !swapchain_ || commandBuffers_.empty()) return;

    vk_.WaitForFences(device_, 1, &inFlight_, VK_TRUE, UINT64_MAX);

    uint32_t index = 0;
    const VkResult acquired = vk_.AcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
                                                      imageAvailable_, VK_NULL_HANDLE, &index);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return;
    }
    if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) return;

    // The uniform write happens after the fence wait so the GPU is no longer
    // reading the buffer the previous frame's command buffer used.
    updateUniforms(elapsed);
    recordFrame(index);

    vk_.ResetFences(device_, 1, &inFlight_);

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &imageAvailable_;
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commandBuffers_[index];
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &renderFinished_;
    if (!ok(vk_.QueueSubmit(queue_, 1, &submit, inFlight_))) return;

    VkPresentInfoKHR present = {};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &renderFinished_;
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    present.pImageIndices = &index;

    const VkResult presented = vk_.QueuePresentKHR(queue_, &present);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR ||
        acquired == VK_SUBOPTIMAL_KHR) {
        recreateSwapchain();
    }
}

void VulkanTool::shutdown() {
    if (!device_) {
        if (instance_ && vk_.DestroyInstance) vk_.DestroyInstance(instance_, nullptr);
        if (vk_.dll) FreeLibrary(vk_.dll);
        return;
    }

    vk_.DeviceWaitIdle(device_);

    if (inFlight_) vk_.DestroyFence(device_, inFlight_, nullptr);
    if (renderFinished_) vk_.DestroySemaphore(device_, renderFinished_, nullptr);
    if (imageAvailable_) vk_.DestroySemaphore(device_, imageAvailable_, nullptr);

    destroySwapchain();

    if (uniformMapped_) {
        vk_.UnmapMemory(device_, uniformMemory_);
        uniformMapped_ = nullptr;
    }
    if (uniformBuffer_) vk_.DestroyBuffer(device_, uniformBuffer_, nullptr);
    if (uniformMemory_) vk_.FreeMemory(device_, uniformMemory_, nullptr);

    if (sampler_) vk_.DestroySampler(device_, sampler_, nullptr);
    if (textureView_) vk_.DestroyImageView(device_, textureView_, nullptr);
    if (texture_) vk_.DestroyImage(device_, texture_, nullptr);
    if (textureMemory_) vk_.FreeMemory(device_, textureMemory_, nullptr);

    if (pool_) vk_.DestroyDescriptorPool(device_, pool_, nullptr);
    if (pipeline_) vk_.DestroyPipeline(device_, pipeline_, nullptr);
    if (pipelineLayout_) vk_.DestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    if (setLayout_) vk_.DestroyDescriptorSetLayout(device_, setLayout_, nullptr);
    if (renderPass_) vk_.DestroyRenderPass(device_, renderPass_, nullptr);
    if (commandPool_) vk_.DestroyCommandPool(device_, commandPool_, nullptr);

    vk_.DestroyDevice(device_, nullptr);
    device_ = VK_NULL_HANDLE;
    if (surface_) vk_.DestroySurfaceKHR(instance_, surface_, nullptr);
    if (instance_) vk_.DestroyInstance(instance_, nullptr);
    if (vk_.dll) FreeLibrary(vk_.dll);
}

Tool* createVulkan() { return new VulkanTool(); }

}  // namespace render
