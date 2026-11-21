#include "zio/render/Device.h"

#if defined(ZIO_PLATFORM_WINDOWS)

#include <windows.h>

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace zio::render
{

namespace
{

VkFormat toVkFormat(VertexFormat format)
{
    switch (format)
    {
    case VertexFormat::Float2:
        return VK_FORMAT_R32G32_SFLOAT;
    case VertexFormat::Float3:
        return VK_FORMAT_R32G32B32_SFLOAT;
    case VertexFormat::Float4:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    }
    return VK_FORMAT_UNDEFINED;
}

}

struct Device::Impl
{
    HWND hwnd = nullptr;

    VkInstance instance             = VK_NULL_HANDLE;
    VkSurfaceKHR surface            = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    std::uint32_t queueFamilyIndex  = 0;
    VkDevice device                 = VK_NULL_HANDLE;
    VkQueue queue                   = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkFramebuffer> framebuffers;

    VkFormat depthFormat       = VK_FORMAT_D32_SFLOAT_S8_UINT;
    VkImage depthImage         = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthView      = VK_NULL_HANDLE;

    std::uint32_t width  = 0;
    std::uint32_t height = 0;

    VkRenderPass renderPassClear = VK_NULL_HANDLE;
    VkRenderPass renderPassLoad  = VK_NULL_HANDLE;

    static constexpr std::uint32_t kShadowMapSize = 2048;
    VkRenderPass renderPassShadow         = VK_NULL_HANDLE;
    VkImage shadowImage                   = VK_NULL_HANDLE;
    VkDeviceMemory shadowMemory           = VK_NULL_HANDLE;
    VkImageView shadowView                = VK_NULL_HANDLE;
    VkFramebuffer shadowFramebuffer       = VK_NULL_HANDLE;
    VkDescriptorSet shadowDescriptorSet   = VK_NULL_HANDLE;
    bool inShadowPass                     = false;
    bool shadowSetBound                   = false;

    VkCommandPool commandPool  = VK_NULL_HANDLE;
    VkCommandBuffer cmd        = VK_NULL_HANDLE;
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    VkFence inFlightFence      = VK_NULL_HANDLE;
    std::uint32_t currentImageIndex = 0;
    bool imageAvailableConsumed = false;

    bool frameValid = false;

    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout            = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool            = VK_NULL_HANDLE;
    VkSampler defaultSampler                   = VK_NULL_HANDLE;
    VkSampler samplers[4]                      = {};

    struct BufferSlot
    {
        VkBuffer buffer                = VK_NULL_HANDLE;
        VkDeviceMemory memory          = VK_NULL_HANDLE;
        BufferUsage usage              = BufferUsage::Immutable;
        std::uint32_t capacity         = 0;
        VkBufferUsageFlags usageFlags  = 0;
    };
    std::vector<BufferSlot> buffers;

    std::vector<std::pair<VkBuffer, VkDeviceMemory>> pendingFrees;

    struct Pipeline
    {
        VkShaderModule vs   = VK_NULL_HANDLE;
        VkShaderModule ps   = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
    };
    std::vector<Pipeline> pipelines;

    struct TextureSlot
    {
        VkImage image                 = VK_NULL_HANDLE;
        VkDeviceMemory memory         = VK_NULL_HANDLE;
        VkImageView view              = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    };
    std::vector<TextureSlot> textures;

    VkPipeline cached_pipeline           = VK_NULL_HANDLE;
    VkBuffer cached_vertex_buffer        = VK_NULL_HANDLE;
    VkDeviceSize cached_vertex_offset    = 0;
    VkDescriptorSet cached_descriptor_set = VK_NULL_HANDLE;

    void resetBoundStateCache()
    {
        cached_pipeline       = VK_NULL_HANDLE;
        cached_vertex_buffer  = VK_NULL_HANDLE;
        cached_vertex_offset  = 0;
        cached_descriptor_set = VK_NULL_HANDLE;
        shadowSetBound        = false;
    }

    bool attachToWindow(HWND externalHwnd)
    {
        hwnd = externalHwnd;
        return hwnd != nullptr;
    }

    bool findMemoryType(std::uint32_t typeBits, VkMemoryPropertyFlags props, std::uint32_t& outIndex) const
    {
        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
        for (std::uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
        {
            if ((typeBits & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props)
            {
                outIndex = i;
                return true;
            }
        }
        return false;
    }

    bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, VkBuffer& outBuffer,
                       VkDeviceMemory& outMemory)
    {
        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.size        = size;
        bufferInfo.usage       = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &bufferInfo, nullptr, &outBuffer) != VK_SUCCESS)
            return false;

        VkMemoryRequirements memReq{};
        vkGetBufferMemoryRequirements(device, outBuffer, &memReq);

        std::uint32_t memType = 0;
        if (!findMemoryType(memReq.memoryTypeBits, props, memType))
        {
            vkDestroyBuffer(device, outBuffer, nullptr);
            outBuffer = VK_NULL_HANDLE;
            return false;
        }

        VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocInfo.allocationSize  = memReq.size;
        allocInfo.memoryTypeIndex = memType;
        if (vkAllocateMemory(device, &allocInfo, nullptr, &outMemory) != VK_SUCCESS)
        {
            vkDestroyBuffer(device, outBuffer, nullptr);
            outBuffer = VK_NULL_HANDLE;
            return false;
        }
        vkBindBufferMemory(device, outBuffer, outMemory, 0);
        return true;
    }

    template <class Fn>
    void runOneShotCommands(Fn&& fn)
    {
        VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocInfo.commandPool        = commandPool;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        VkCommandBuffer oneShot      = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(device, &allocInfo, &oneShot) != VK_SUCCESS)
            return;

        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(oneShot, &beginInfo);
        fn(oneShot);
        vkEndCommandBuffer(oneShot);

        VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers    = &oneShot;
        vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(queue);

        vkFreeCommandBuffers(device, commandPool, 1, &oneShot);
    }

    bool createInstance()
    {
        VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        appInfo.pApplicationName = "Motor_zio";
        appInfo.apiVersion = VK_API_VERSION_1_1;

        const char* extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};

        const char* validationLayer = "VK_LAYER_KHRONOS_validation";
        std::uint32_t layerCount    = 0;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> layers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, layers.data());
        bool haveValidation = false;
        for (const auto& layer : layers)
            if (std::strcmp(layer.layerName, validationLayer) == 0)
                haveValidation = true;

        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        info.pApplicationInfo        = &appInfo;
        info.enabledExtensionCount   = 2;
        info.ppEnabledExtensionNames = extensions;
        if (haveValidation)
        {
            info.enabledLayerCount   = 1;
            info.ppEnabledLayerNames = &validationLayer;
        }

        return vkCreateInstance(&info, nullptr, &instance) == VK_SUCCESS;
    }

    bool createSurfaceFromWindow()
    {
        VkWin32SurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
        info.hinstance = GetModuleHandleW(nullptr);
        info.hwnd      = hwnd;
        return vkCreateWin32SurfaceKHR(instance, &info, nullptr, &surface) == VK_SUCCESS;
    }

    bool pickPhysicalDeviceAndQueueFamily()
    {
        std::uint32_t count = 0;
        vkEnumeratePhysicalDevices(instance, &count, nullptr);
        if (count == 0)
            return false;
        std::vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(instance, &count, devices.data());

        for (VkPhysicalDevice candidate : devices)
        {
            std::uint32_t qCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &qCount, nullptr);
            std::vector<VkQueueFamilyProperties> qProps(qCount);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &qCount, qProps.data());

            for (std::uint32_t i = 0; i < qCount; ++i)
            {
                VkBool32 presentSupport = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &presentSupport);
                if ((qProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupport)
                {
                    physicalDevice   = candidate;
                    queueFamilyIndex = i;
                    return true;
                }
            }
        }
        return false;
    }

    bool createLogicalDeviceAndQueue()
    {
        float priority = 1.f;
        VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = queueFamilyIndex;
        queueInfo.queueCount       = 1;
        queueInfo.pQueuePriorities = &priority;

        const char* deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

        VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        deviceInfo.queueCreateInfoCount    = 1;
        deviceInfo.pQueueCreateInfos       = &queueInfo;
        deviceInfo.enabledExtensionCount   = 1;
        deviceInfo.ppEnabledExtensionNames = deviceExtensions;

        if (vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device) != VK_SUCCESS)
            return false;
        vkGetDeviceQueue(device, queueFamilyIndex, 0, &queue);
        return true;
    }

    bool createSwapchain(std::uint32_t w, std::uint32_t h)
    {
        VkSurfaceCapabilitiesKHR caps{};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &caps);

        std::uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());

        swapchainFormat        = formats[0].format;
        VkColorSpaceKHR colorSpace = formats[0].colorSpace;
        for (const auto& f : formats)
        {
            if (f.format == VK_FORMAT_R8G8B8A8_UNORM)
            {
                swapchainFormat = f.format;
                colorSpace      = f.colorSpace;
                break;
            }
            if (f.format == VK_FORMAT_B8G8R8A8_UNORM)
            {
                swapchainFormat = f.format;
                colorSpace      = f.colorSpace;
            }
        }

        std::uint32_t w2 = w;
        std::uint32_t h2 = h;
        if (caps.currentExtent.width != 0xFFFFFFFFu)
        {
            w2 = caps.currentExtent.width;
            h2 = caps.currentExtent.height;
        }
        w2 = std::clamp(w2, caps.minImageExtent.width, caps.maxImageExtent.width);
        h2 = std::clamp(h2, caps.minImageExtent.height, caps.maxImageExtent.height);

        std::uint32_t imageCount = caps.minImageCount + 1;
        if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
            imageCount = caps.maxImageCount;

        VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        info.surface          = surface;
        info.minImageCount    = imageCount;
        info.imageFormat      = swapchainFormat;
        info.imageColorSpace  = colorSpace;
        info.imageExtent      = {w2, h2};
        info.imageArrayLayers = 1;
        info.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.preTransform     = caps.currentTransform;
        info.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        info.presentMode      = VK_PRESENT_MODE_FIFO_KHR;
        info.clipped          = VK_TRUE;

        const VkResult swapchainResult = vkCreateSwapchainKHR(device, &info, nullptr, &swapchain);
        if (swapchainResult != VK_SUCCESS)
        {
            std::fprintf(stderr, "[render] vkCreateSwapchainKHR failed: %d (extent %ux%u)\n", swapchainResult, w2, h2);
            return false;
        }

        std::uint32_t actualCount = 0;
        vkGetSwapchainImagesKHR(device, swapchain, &actualCount, nullptr);
        swapchainImages.resize(actualCount);
        vkGetSwapchainImagesKHR(device, swapchain, &actualCount, swapchainImages.data());

        swapchainImageViews.resize(actualCount);
        for (std::uint32_t i = 0; i < actualCount; ++i)
        {
            VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            viewInfo.image                           = swapchainImages[i];
            viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format                          = swapchainFormat;
            viewInfo.subresourceRange                = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            if (vkCreateImageView(device, &viewInfo, nullptr, &swapchainImageViews[i]) != VK_SUCCESS)
                return false;
        }

        renderFinishedSemaphores.resize(actualCount);
        for (auto& sem : renderFinishedSemaphores)
        {
            VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            if (vkCreateSemaphore(device, &semInfo, nullptr, &sem) != VK_SUCCESS)
                return false;
        }

        width  = w2;
        height = h2;
        return true;
    }

    void pickDepthFormat()
    {
        for (const VkFormat f : {VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT})
        {
            VkFormatProperties props{};
            vkGetPhysicalDeviceFormatProperties(physicalDevice, f, &props);
            if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            {
                depthFormat = f;
                return;
            }
        }
    }

    bool createRenderPasses()
    {
        pickDepthFormat();
        VkAttachmentDescription attachments[2]{};
        VkAttachmentDescription& colorAttachment = attachments[0];
        VkAttachmentDescription& depthAttachment = attachments[1];
        colorAttachment.format         = swapchainFormat;
        colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        depthAttachment.format         = depthFormat;
        depthAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount    = 1;
        subpass.pColorAttachments       = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;

        constexpr VkPipelineStageFlags kStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                                 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                                 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        VkSubpassDependency dependency{};
        dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass    = 0;
        dependency.srcStageMask  = kStages;
        dependency.dstStageMask  = kStages;
        dependency.srcAccessMask = 0;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rpInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rpInfo.attachmentCount = 2;
        rpInfo.pAttachments    = attachments;
        rpInfo.subpassCount    = 1;
        rpInfo.pSubpasses      = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies   = &dependency;

        colorAttachment.loadOp        = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateRenderPass(device, &rpInfo, nullptr, &renderPassClear) != VK_SUCCESS)
            return false;

        colorAttachment.loadOp        = VK_ATTACHMENT_LOAD_OP_LOAD;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
        depthAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_LOAD;
        depthAttachment.initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        if (vkCreateRenderPass(device, &rpInfo, nullptr, &renderPassLoad) != VK_SUCCESS)
            return false;

        return true;
    }

    bool createDepthResources()
    {
        VkImageCreateInfo imgInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imgInfo.imageType     = VK_IMAGE_TYPE_2D;
        imgInfo.format        = depthFormat;
        imgInfo.extent        = {width, height, 1};
        imgInfo.mipLevels     = 1;
        imgInfo.arrayLayers   = 1;
        imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
        imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        imgInfo.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(device, &imgInfo, nullptr, &depthImage) != VK_SUCCESS)
            return false;

        VkMemoryRequirements memReq{};
        vkGetImageMemoryRequirements(device, depthImage, &memReq);
        std::uint32_t memType = 0;
        if (!findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memType))
            return false;
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize  = memReq.size;
        alloc.memoryTypeIndex = memType;
        if (vkAllocateMemory(device, &alloc, nullptr, &depthMemory) != VK_SUCCESS)
            return false;
        vkBindImageMemory(device, depthImage, depthMemory, 0);

        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image                       = depthImage;
        viewInfo.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format                      = depthFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        return vkCreateImageView(device, &viewInfo, nullptr, &depthView) == VK_SUCCESS;
    }

    bool createFramebuffers()
    {
        if (!createDepthResources())
            return false;

        framebuffers.resize(swapchainImageViews.size());
        for (std::size_t i = 0; i < swapchainImageViews.size(); ++i)
        {
            const VkImageView fbAttachments[2] = {swapchainImageViews[i], depthView};
            VkFramebufferCreateInfo fbInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fbInfo.renderPass      = renderPassClear;
            fbInfo.attachmentCount = 2;
            fbInfo.pAttachments    = fbAttachments;
            fbInfo.width           = width;
            fbInfo.height          = height;
            fbInfo.layers          = 1;
            if (vkCreateFramebuffer(device, &fbInfo, nullptr, &framebuffers[i]) != VK_SUCCESS)
                return false;
        }
        return true;
    }

    bool createCommandPoolAndBuffer()
    {
        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queueFamilyIndex;
        if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS)
            return false;

        VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocInfo.commandPool        = commandPool;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        return vkAllocateCommandBuffers(device, &allocInfo, &cmd) == VK_SUCCESS;
    }

    bool createSyncObjects()
    {
        VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        return vkCreateSemaphore(device, &semInfo, nullptr, &imageAvailable) == VK_SUCCESS &&
               vkCreateFence(device, &fenceInfo, nullptr, &inFlightFence) == VK_SUCCESS;
    }

    bool createDescriptorLayoutAndPool()
    {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding         = 0;
        binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings    = &binding;
        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS)
            return false;

        const VkDescriptorSetLayout setLayouts[2] = {descriptorSetLayout, descriptorSetLayout};
        VkPipelineLayoutCreateInfo plInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        plInfo.setLayoutCount = 2;
        plInfo.pSetLayouts    = setLayouts;
        if (vkCreatePipelineLayout(device, &plInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
            return false;

        static constexpr std::uint32_t kMaxDescriptorSets = 4096;
        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxDescriptorSets};
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets       = kMaxDescriptorSets;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes    = &poolSize;
        return vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) == VK_SUCCESS;
    }

    bool createDefaultSampler()
    {
        VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        info.magFilter    = VK_FILTER_NEAREST;
        info.minFilter    = VK_FILTER_NEAREST;
        info.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.maxLod       = VK_LOD_CLAMP_NONE;
        for (int i = 0; i < 4; ++i)
        {
            const bool linear = (i & 1) != 0;
            const bool repeat = (i & 2) != 0;
            info.magFilter    = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
            info.minFilter    = info.magFilter;
            info.mipmapMode   = linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
            info.addressModeU = info.addressModeV = info.addressModeW =
                repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            if (vkCreateSampler(device, &info, nullptr, &samplers[i]) != VK_SUCCESS)
                return false;
        }
        defaultSampler = samplers[0];
        return true;
    }

    VkImage dummyImage                 = VK_NULL_HANDLE;
    VkDeviceMemory dummyImageMemory     = VK_NULL_HANDLE;
    VkImageView dummyImageView          = VK_NULL_HANDLE;
    VkDescriptorSet dummyDescriptorSet  = VK_NULL_HANDLE;

    bool createDummyTexture()
    {
        const std::uint8_t whitePixel[4] = {255, 255, 255, 255};

        VkBuffer stagingBuffer       = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        if (!createBuffer(4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer,
                           stagingMemory))
            return false;

        void* mapped = nullptr;
        vkMapMemory(device, stagingMemory, 0, 4, 0, &mapped);
        std::memcpy(mapped, whitePixel, 4);
        vkUnmapMemory(device, stagingMemory);

        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType     = VK_IMAGE_TYPE_2D;
        imageInfo.extent        = {1, 1, 1};
        imageInfo.mipLevels     = 1;
        imageInfo.arrayLayers   = 1;
        imageInfo.format        = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
        if (vkCreateImage(device, &imageInfo, nullptr, &dummyImage) != VK_SUCCESS)
        {
            vkDestroyBuffer(device, stagingBuffer, nullptr);
            vkFreeMemory(device, stagingMemory, nullptr);
            return false;
        }

        VkMemoryRequirements memReq{};
        vkGetImageMemoryRequirements(device, dummyImage, &memReq);
        std::uint32_t memType = 0;
        if (!findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memType))
        {
            vkDestroyImage(device, dummyImage, nullptr);
            vkDestroyBuffer(device, stagingBuffer, nullptr);
            vkFreeMemory(device, stagingMemory, nullptr);
            return false;
        }
        VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocInfo.allocationSize  = memReq.size;
        allocInfo.memoryTypeIndex = memType;
        if (vkAllocateMemory(device, &allocInfo, nullptr, &dummyImageMemory) != VK_SUCCESS)
        {
            vkDestroyImage(device, dummyImage, nullptr);
            vkDestroyBuffer(device, stagingBuffer, nullptr);
            vkFreeMemory(device, stagingMemory, nullptr);
            return false;
        }
        vkBindImageMemory(device, dummyImage, dummyImageMemory, 0);

        runOneShotCommands([&](VkCommandBuffer cmd) {
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image               = dummyImage;
            barrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

            barrier.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                                  nullptr, 0, nullptr, 1, &barrier);

            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent      = {1, 1, 1};
            vkCmdCopyBufferToImage(cmd, stagingBuffer, dummyImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                                  nullptr, 0, nullptr, 1, &barrier);
        });

        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingMemory, nullptr);

        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image             = dummyImage;
        viewInfo.viewType          = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format            = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange  = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(device, &viewInfo, nullptr, &dummyImageView) != VK_SUCCESS)
            return false;

        VkDescriptorSetAllocateInfo dsAlloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        dsAlloc.descriptorPool     = descriptorPool;
        dsAlloc.descriptorSetCount = 1;
        dsAlloc.pSetLayouts        = &descriptorSetLayout;
        if (vkAllocateDescriptorSets(device, &dsAlloc, &dummyDescriptorSet) != VK_SUCCESS)
            return false;

        VkDescriptorImageInfo imageDescInfo{};
        imageDescInfo.sampler     = defaultSampler;
        imageDescInfo.imageView   = dummyImageView;
        imageDescInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet          = dummyDescriptorSet;
        write.dstBinding      = 0;
        write.descriptorCount = 1;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo      = &imageDescInfo;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        return true;
    }

    bool createShadowResources()
    {
        constexpr VkFormat kFormat = VK_FORMAT_D32_SFLOAT;
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, kFormat, &props);
        constexpr VkFormatFeatureFlags kNeeded =
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if ((props.optimalTilingFeatures & kNeeded) != kNeeded)
            return false;

        VkAttachmentDescription depth{};
        depth.format         = kFormat;
        depth.samples        = VK_SAMPLE_COUNT_1_BIT;
        depth.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        depth.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        depth.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference depthRef{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.pDepthStencilAttachment = &depthRef;

        constexpr VkPipelineStageFlags kDepthStages =
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        VkSubpassDependency deps[2]{};
        deps[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass    = 0;
        deps[0].srcStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[0].dstStageMask  = kDepthStages;
        deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[1].srcSubpass    = 0;
        deps[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask  = kDepthStages;
        deps[1].dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo rpInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments    = &depth;
        rpInfo.subpassCount    = 1;
        rpInfo.pSubpasses      = &subpass;
        rpInfo.dependencyCount = 2;
        rpInfo.pDependencies   = deps;
        if (vkCreateRenderPass(device, &rpInfo, nullptr, &renderPassShadow) != VK_SUCCESS)
            return false;

        VkImageCreateInfo imgInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imgInfo.imageType     = VK_IMAGE_TYPE_2D;
        imgInfo.format        = kFormat;
        imgInfo.extent        = {kShadowMapSize, kShadowMapSize, 1};
        imgInfo.mipLevels     = 1;
        imgInfo.arrayLayers   = 1;
        imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
        imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        imgInfo.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(device, &imgInfo, nullptr, &shadowImage) != VK_SUCCESS)
            return false;

        VkMemoryRequirements memReq{};
        vkGetImageMemoryRequirements(device, shadowImage, &memReq);
        std::uint32_t memType = 0;
        if (!findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memType))
            return false;
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize  = memReq.size;
        alloc.memoryTypeIndex = memType;
        if (vkAllocateMemory(device, &alloc, nullptr, &shadowMemory) != VK_SUCCESS)
            return false;
        vkBindImageMemory(device, shadowImage, shadowMemory, 0);

        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image                       = shadowImage;
        viewInfo.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format                      = kFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &viewInfo, nullptr, &shadowView) != VK_SUCCESS)
            return false;

        VkFramebufferCreateInfo fbInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fbInfo.renderPass      = renderPassShadow;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments    = &shadowView;
        fbInfo.width           = kShadowMapSize;
        fbInfo.height          = kShadowMapSize;
        fbInfo.layers          = 1;
        if (vkCreateFramebuffer(device, &fbInfo, nullptr, &shadowFramebuffer) != VK_SUCCESS)
            return false;

        runOneShotCommands([&](VkCommandBuffer one) {
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image               = shadowImage;
            barrier.subresourceRange    = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
            barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(one, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                                 nullptr, 0, nullptr, 1, &barrier);
        });

        VkDescriptorSetAllocateInfo dsAlloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        dsAlloc.descriptorPool     = descriptorPool;
        dsAlloc.descriptorSetCount = 1;
        dsAlloc.pSetLayouts        = &descriptorSetLayout;
        if (vkAllocateDescriptorSets(device, &dsAlloc, &shadowDescriptorSet) != VK_SUCCESS)
            return false;

        VkDescriptorImageInfo imageInfo{};
        imageInfo.sampler     = samplers[0];
        imageInfo.imageView   = shadowView;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet          = shadowDescriptorSet;
        write.dstBinding      = 0;
        write.descriptorCount = 1;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo      = &imageInfo;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        return true;
    }

    void destroyShadowResources()
    {
        if (shadowFramebuffer)
            vkDestroyFramebuffer(device, shadowFramebuffer, nullptr);
        if (shadowView)
            vkDestroyImageView(device, shadowView, nullptr);
        if (shadowImage)
            vkDestroyImage(device, shadowImage, nullptr);
        if (shadowMemory)
            vkFreeMemory(device, shadowMemory, nullptr);
        if (renderPassShadow)
            vkDestroyRenderPass(device, renderPassShadow, nullptr);
        shadowFramebuffer = VK_NULL_HANDLE;
        shadowView        = VK_NULL_HANDLE;
        shadowImage       = VK_NULL_HANDLE;
        shadowMemory      = VK_NULL_HANDLE;
        renderPassShadow  = VK_NULL_HANDLE;
    }

    void destroySwapchainResources()
    {
        for (auto sem : renderFinishedSemaphores)
            vkDestroySemaphore(device, sem, nullptr);
        renderFinishedSemaphores.clear();
        for (auto fb : framebuffers)
            vkDestroyFramebuffer(device, fb, nullptr);
        framebuffers.clear();
        if (depthView)
            vkDestroyImageView(device, depthView, nullptr);
        if (depthImage)
            vkDestroyImage(device, depthImage, nullptr);
        if (depthMemory)
            vkFreeMemory(device, depthMemory, nullptr);
        depthView   = VK_NULL_HANDLE;
        depthImage  = VK_NULL_HANDLE;
        depthMemory = VK_NULL_HANDLE;
        for (auto view : swapchainImageViews)
            vkDestroyImageView(device, view, nullptr);
        swapchainImageViews.clear();
        swapchainImages.clear();
        if (swapchain)
        {
            vkDestroySwapchainKHR(device, swapchain, nullptr);
            swapchain = VK_NULL_HANDLE;
        }
    }

    void beginRenderPass(VkRenderPass rp, const ClearColor& clear)
    {
        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkBeginCommandBuffer(cmd, &beginInfo);
        recordMainPass(rp, clear);
    }

    void recordMainPass(VkRenderPass rp, const ClearColor& clear)
    {
        VkClearValue clearValues[2]{};
        clearValues[0].color        = {{clear.r, clear.g, clear.b, clear.a}};
        clearValues[1].depthStencil = {1.f, 0};

        VkRenderPassBeginInfo rpInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rpInfo.renderPass        = rp;
        rpInfo.framebuffer       = framebuffers[currentImageIndex];
        rpInfo.renderArea.extent = {width, height};
        rpInfo.clearValueCount   = 2;
        rpInfo.pClearValues      = clearValues;
        vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{0.f, static_cast<float>(height), static_cast<float>(width), -static_cast<float>(height),
                             0.f, 1.f};
        VkRect2D scissor{{0, 0}, {width, height}};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        resetBoundStateCache();
    }
};

Device::Device() : impl_(std::make_unique<Impl>()) {}
Device::~Device() = default;

Device& Device::instance()
{
    static Device global;
    return global;
}

bool Device::init(void* nativeWindow, std::uint32_t width, std::uint32_t height)
{
    if (!impl_->attachToWindow(static_cast<HWND>(nativeWindow)))
        return false;

    return impl_->createInstance() && impl_->createSurfaceFromWindow() && impl_->pickPhysicalDeviceAndQueueFamily() &&
           impl_->createLogicalDeviceAndQueue() && impl_->createSwapchain(width, height) &&
           impl_->createRenderPasses() && impl_->createFramebuffers() && impl_->createCommandPoolAndBuffer() &&
           impl_->createSyncObjects() && impl_->createDescriptorLayoutAndPool() && impl_->createDefaultSampler() &&
           impl_->createDummyTexture() && impl_->createShadowResources();
}

void Device::shutdown()
{
    if (impl_->device)
        vkDeviceWaitIdle(impl_->device);

    impl_->resetBoundStateCache();

    for (auto& [buf, mem] : impl_->pendingFrees)
    {
        vkDestroyBuffer(impl_->device, buf, nullptr);
        vkFreeMemory(impl_->device, mem, nullptr);
    }
    impl_->pendingFrees.clear();

    if (impl_->dummyImageView)
        vkDestroyImageView(impl_->device, impl_->dummyImageView, nullptr);
    if (impl_->dummyImage)
        vkDestroyImage(impl_->device, impl_->dummyImage, nullptr);
    if (impl_->dummyImageMemory)
        vkFreeMemory(impl_->device, impl_->dummyImageMemory, nullptr);

    for (auto& tex : impl_->textures)
    {
        if (tex.view)
            vkDestroyImageView(impl_->device, tex.view, nullptr);
        if (tex.image)
            vkDestroyImage(impl_->device, tex.image, nullptr);
        if (tex.memory)
            vkFreeMemory(impl_->device, tex.memory, nullptr);
    }
    impl_->textures.clear();

    for (auto& pipe : impl_->pipelines)
    {
        if (pipe.pipeline)
            vkDestroyPipeline(impl_->device, pipe.pipeline, nullptr);
        if (pipe.vs)
            vkDestroyShaderModule(impl_->device, pipe.vs, nullptr);
        if (pipe.ps)
            vkDestroyShaderModule(impl_->device, pipe.ps, nullptr);
    }
    impl_->pipelines.clear();

    for (auto& buf : impl_->buffers)
    {
        if (buf.buffer)
            vkDestroyBuffer(impl_->device, buf.buffer, nullptr);
        if (buf.memory)
            vkFreeMemory(impl_->device, buf.memory, nullptr);
    }
    impl_->buffers.clear();

    for (VkSampler s : impl_->samplers)
        if (s)
            vkDestroySampler(impl_->device, s, nullptr);
    if (impl_->descriptorPool)
        vkDestroyDescriptorPool(impl_->device, impl_->descriptorPool, nullptr);
    if (impl_->pipelineLayout)
        vkDestroyPipelineLayout(impl_->device, impl_->pipelineLayout, nullptr);
    if (impl_->descriptorSetLayout)
        vkDestroyDescriptorSetLayout(impl_->device, impl_->descriptorSetLayout, nullptr);

    if (impl_->inFlightFence)
        vkDestroyFence(impl_->device, impl_->inFlightFence, nullptr);
    if (impl_->imageAvailable)
        vkDestroySemaphore(impl_->device, impl_->imageAvailable, nullptr);

    if (impl_->commandPool)
        vkDestroyCommandPool(impl_->device, impl_->commandPool, nullptr);

    impl_->destroySwapchainResources();
    impl_->destroyShadowResources();
    if (impl_->renderPassLoad)
        vkDestroyRenderPass(impl_->device, impl_->renderPassLoad, nullptr);
    if (impl_->renderPassClear)
        vkDestroyRenderPass(impl_->device, impl_->renderPassClear, nullptr);

    if (impl_->surface)
        vkDestroySurfaceKHR(impl_->instance, impl_->surface, nullptr);
    if (impl_->device)
        vkDestroyDevice(impl_->device, nullptr);
    if (impl_->instance)
        vkDestroyInstance(impl_->instance, nullptr);

    *impl_ = Impl{};
}

bool Device::isInitialized() const
{
    return impl_->device != VK_NULL_HANDLE;
}

bool Device::resize(std::uint32_t width, std::uint32_t height)
{
    if (!impl_->device || width == 0 || height == 0)
        return false;

    vkDeviceWaitIdle(impl_->device);
    impl_->destroySwapchainResources();

    if (!impl_->createSwapchain(width, height))
        return false;
    return impl_->createFramebuffers();
}

BufferHandle Device::createBuffer(const BufferDesc& desc)
{
    const VkBufferUsageFlags usage =
        desc.type == BufferType::Vertex ? VK_BUFFER_USAGE_VERTEX_BUFFER_BIT : VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

    VkBuffer buffer       = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (!impl_->createBuffer(desc.sizeBytes, usage,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, buffer,
                              memory))
        return kInvalidHandle;

    if (desc.data)
    {
        void* mapped = nullptr;
        vkMapMemory(impl_->device, memory, 0, desc.sizeBytes, 0, &mapped);
        std::memcpy(mapped, desc.data, desc.sizeBytes);
        vkUnmapMemory(impl_->device, memory);
    }

    impl_->buffers.push_back(
        Impl::BufferSlot{buffer, memory, desc.usage, static_cast<std::uint32_t>(desc.sizeBytes), usage});
    return static_cast<BufferHandle>(impl_->buffers.size());
}

void Device::destroyBuffer(BufferHandle handle)
{
    if (handle == kInvalidHandle || handle > impl_->buffers.size())
        return;
    auto& slot = impl_->buffers[handle - 1];
    if (slot.buffer == impl_->cached_vertex_buffer)
    {
        impl_->cached_vertex_buffer = VK_NULL_HANDLE;
        impl_->cached_vertex_offset = 0;
    }
    if (slot.buffer)
        vkDestroyBuffer(impl_->device, slot.buffer, nullptr);
    if (slot.memory)
        vkFreeMemory(impl_->device, slot.memory, nullptr);
    slot = Impl::BufferSlot{};
}

bool Device::updateBuffer(BufferHandle handle, const void* data, std::uint32_t sizeBytes)
{
    if (handle == kInvalidHandle || handle > impl_->buffers.size())
        return false;

    auto& slot = impl_->buffers[handle - 1];
    if (slot.usage != BufferUsage::Dynamic || !slot.buffer || sizeBytes > slot.capacity)
        return false;

    VkBuffer newBuffer       = VK_NULL_HANDLE;
    VkDeviceMemory newMemory = VK_NULL_HANDLE;
    if (!impl_->createBuffer(slot.capacity, slot.usageFlags,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, newBuffer,
                              newMemory))
        return false;

    void* mapped = nullptr;
    vkMapMemory(impl_->device, newMemory, 0, sizeBytes, 0, &mapped);
    std::memcpy(mapped, data, sizeBytes);
    vkUnmapMemory(impl_->device, newMemory);

    impl_->pendingFrees.emplace_back(slot.buffer, slot.memory);
    if (slot.buffer == impl_->cached_vertex_buffer)
    {
        impl_->cached_vertex_buffer = VK_NULL_HANDLE;
        impl_->cached_vertex_offset = 0;
    }
    slot.buffer = newBuffer;
    slot.memory = newMemory;
    return true;
}

PipelineHandle Device::createPipeline(const PipelineDesc& desc)
{
    if (desc.vertexShaderSpirv.empty() || desc.pixelShaderSpirv.empty())
        return kInvalidHandle;

    auto makeShaderModule = [this](std::span<const std::uint32_t> spirv) -> VkShaderModule {
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize          = spirv.size_bytes();
        info.pCode             = spirv.data();
        VkShaderModule module = VK_NULL_HANDLE;
        vkCreateShaderModule(impl_->device, &info, nullptr, &module);
        return module;
    };

    Impl::Pipeline pipeline{};
    pipeline.vs = makeShaderModule(desc.vertexShaderSpirv);
    pipeline.ps = makeShaderModule(desc.pixelShaderSpirv);
    if (!pipeline.vs || !pipeline.ps)
    {
        if (pipeline.vs)
            vkDestroyShaderModule(impl_->device, pipeline.vs, nullptr);
        if (pipeline.ps)
            vkDestroyShaderModule(impl_->device, pipeline.ps, nullptr);
        return kInvalidHandle;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0]        = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = pipeline.vs;
    stages[0].pName  = "main";
    stages[1]        = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = pipeline.ps;
    stages[1].pName  = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = desc.vertexStrideBytes;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector<VkVertexInputAttributeDescription> attributes;
    attributes.reserve(desc.attributeCount);
    for (std::size_t i = 0; i < desc.attributeCount; ++i)
    {
        const auto& attr = desc.attributes[i];
        attributes.push_back(
            {static_cast<std::uint32_t>(i), 0, toVkFormat(attr.format), attr.offsetBytes});
    }

    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount   = 1;
    vertexInput.pVertexBindingDescriptions      = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions    = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount  = 1;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates    = dynamicStates;

    VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode    = desc.cullBack ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
    rasterizer.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth   = 1.f;

    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable         = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp        = VK_BLEND_OP_ADD;
    blendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlend.attachmentCount = desc.shadowPass ? 0 : 1;
    colorBlend.pAttachments    = desc.shadowPass ? nullptr : &blendAttachment;

    if (desc.shadowPass)
    {
        rasterizer.cullMode               = VK_CULL_MODE_NONE;
        rasterizer.depthBiasEnable        = VK_TRUE;
        rasterizer.depthBiasConstantFactor = 1.25f;
        rasterizer.depthBiasSlopeFactor    = 1.75f;
    }

    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable  = desc.depthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = (desc.depthTest && desc.depthWrite) ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;
    if (desc.stencilOnce)
    {
        VkStencilOpState once{};
        once.failOp      = VK_STENCIL_OP_KEEP;
        once.passOp      = VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        once.depthFailOp = VK_STENCIL_OP_KEEP;
        once.compareOp   = VK_COMPARE_OP_EQUAL;
        once.compareMask = 0xFF;
        once.writeMask   = 0xFF;
        once.reference   = 0;
        depthStencil.stencilTestEnable = VK_TRUE;
        depthStencil.front             = once;
        depthStencil.back              = once;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.stageCount          = 2;
    pipelineInfo.pStages             = stages;
    pipelineInfo.pVertexInputState   = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState      = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState   = &multisample;
    pipelineInfo.pDepthStencilState  = &depthStencil;
    pipelineInfo.pColorBlendState    = &colorBlend;
    pipelineInfo.pDynamicState       = &dynamicState;
    pipelineInfo.layout              = impl_->pipelineLayout;
    pipelineInfo.renderPass          = desc.shadowPass ? impl_->renderPassShadow : impl_->renderPassClear;
    pipelineInfo.subpass             = 0;

    if (vkCreateGraphicsPipelines(impl_->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline.pipeline) !=
        VK_SUCCESS)
    {
        vkDestroyShaderModule(impl_->device, pipeline.vs, nullptr);
        vkDestroyShaderModule(impl_->device, pipeline.ps, nullptr);
        return kInvalidHandle;
    }

    impl_->pipelines.push_back(pipeline);
    return static_cast<PipelineHandle>(impl_->pipelines.size());
}

void Device::destroyPipeline(PipelineHandle handle)
{
    if (handle == kInvalidHandle || handle > impl_->pipelines.size())
        return;
    auto& pipe = impl_->pipelines[handle - 1];
    if (pipe.pipeline == impl_->cached_pipeline)
        impl_->cached_pipeline = VK_NULL_HANDLE;
    if (pipe.pipeline)
        vkDestroyPipeline(impl_->device, pipe.pipeline, nullptr);
    if (pipe.vs)
        vkDestroyShaderModule(impl_->device, pipe.vs, nullptr);
    if (pipe.ps)
        vkDestroyShaderModule(impl_->device, pipe.ps, nullptr);
    pipe = Impl::Pipeline{};
}

TextureHandle Device::createTexture(const TextureDesc& desc)
{
    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(desc.width) * desc.height * 4;

    VkBuffer stagingBuffer        = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory  = VK_NULL_HANDLE;
    if (!impl_->createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              stagingBuffer, stagingMemory))
        return kInvalidHandle;

    void* mapped = nullptr;
    vkMapMemory(impl_->device, stagingMemory, 0, imageSize, 0, &mapped);
    std::memcpy(mapped, desc.pixelsRgba8, static_cast<std::size_t>(imageSize));
    vkUnmapMemory(impl_->device, stagingMemory);

    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType     = VK_IMAGE_TYPE_2D;
    imageInfo.extent        = {desc.width, desc.height, 1};
    imageInfo.mipLevels     = 1;
    imageInfo.arrayLayers   = 1;
    imageInfo.format        = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;

    VkImage image = VK_NULL_HANDLE;
    if (vkCreateImage(impl_->device, &imageInfo, nullptr, &image) != VK_SUCCESS)
    {
        vkDestroyBuffer(impl_->device, stagingBuffer, nullptr);
        vkFreeMemory(impl_->device, stagingMemory, nullptr);
        return kInvalidHandle;
    }

    VkMemoryRequirements memReq{};
    vkGetImageMemoryRequirements(impl_->device, image, &memReq);
    std::uint32_t memType = 0;
    if (!impl_->findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memType))
    {
        vkDestroyImage(impl_->device, image, nullptr);
        vkDestroyBuffer(impl_->device, stagingBuffer, nullptr);
        vkFreeMemory(impl_->device, stagingMemory, nullptr);
        return kInvalidHandle;
    }

    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize  = memReq.size;
    allocInfo.memoryTypeIndex = memType;
    VkDeviceMemory imageMemory = VK_NULL_HANDLE;
    if (vkAllocateMemory(impl_->device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS)
    {
        vkDestroyImage(impl_->device, image, nullptr);
        vkDestroyBuffer(impl_->device, stagingBuffer, nullptr);
        vkFreeMemory(impl_->device, stagingMemory, nullptr);
        return kInvalidHandle;
    }
    vkBindImageMemory(impl_->device, image, imageMemory, 0);

    impl_->runOneShotCommands([&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = image;
        barrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        barrier.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                              nullptr, 1, &barrier);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent      = {desc.width, desc.height, 1};
        vkCmdCopyBufferToImage(cmd, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                              nullptr, 0, nullptr, 1, &barrier);
    });

    vkDestroyBuffer(impl_->device, stagingBuffer, nullptr);
    vkFreeMemory(impl_->device, stagingMemory, nullptr);

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image                = image;
    viewInfo.viewType             = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format               = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange     = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageView view = VK_NULL_HANDLE;
    if (vkCreateImageView(impl_->device, &viewInfo, nullptr, &view) != VK_SUCCESS)
    {
        vkDestroyImage(impl_->device, image, nullptr);
        vkFreeMemory(impl_->device, imageMemory, nullptr);
        return kInvalidHandle;
    }

    VkDescriptorSetAllocateInfo dsAlloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsAlloc.descriptorPool     = impl_->descriptorPool;
    dsAlloc.descriptorSetCount = 1;
    dsAlloc.pSetLayouts        = &impl_->descriptorSetLayout;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(impl_->device, &dsAlloc, &descriptorSet) != VK_SUCCESS)
    {
        vkDestroyImageView(impl_->device, view, nullptr);
        vkDestroyImage(impl_->device, image, nullptr);
        vkFreeMemory(impl_->device, imageMemory, nullptr);
        return kInvalidHandle;
    }

    VkDescriptorImageInfo imageDescInfo{};
    imageDescInfo.sampler     = impl_->samplers[(desc.filter == TextureFilter::Linear ? 1 : 0) |
                                                (desc.wrap == TextureWrap::Repeat ? 2 : 0)];
    imageDescInfo.imageView   = view;
    imageDescInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet          = descriptorSet;
    write.dstBinding      = 0;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo      = &imageDescInfo;
    vkUpdateDescriptorSets(impl_->device, 1, &write, 0, nullptr);

    impl_->textures.push_back(Impl::TextureSlot{image, imageMemory, view, descriptorSet});
    return static_cast<TextureHandle>(impl_->textures.size());
}

void Device::destroyTexture(TextureHandle handle)
{
    if (handle == kInvalidHandle || handle > impl_->textures.size())
        return;
    auto& tex = impl_->textures[handle - 1];
    if (tex.descriptorSet == impl_->cached_descriptor_set)
        impl_->cached_descriptor_set = VK_NULL_HANDLE;
    if (tex.view)
        vkDestroyImageView(impl_->device, tex.view, nullptr);
    if (tex.image)
        vkDestroyImage(impl_->device, tex.image, nullptr);
    if (tex.memory)
        vkFreeMemory(impl_->device, tex.memory, nullptr);
    tex = Impl::TextureSlot{};
}

void Device::beginFrame(const ClearColor& clear)
{
    impl_->frameValid = false;
    if (!impl_->device)
        return;

    vkWaitForFences(impl_->device, 1, &impl_->inFlightFence, VK_TRUE, UINT64_MAX);

    for (auto& [buf, mem] : impl_->pendingFrees)
    {
        vkDestroyBuffer(impl_->device, buf, nullptr);
        vkFreeMemory(impl_->device, mem, nullptr);
    }
    impl_->pendingFrees.clear();

    const VkResult acquireResult = vkAcquireNextImageKHR(impl_->device, impl_->swapchain, UINT64_MAX,
                                                          impl_->imageAvailable, VK_NULL_HANDLE,
                                                          &impl_->currentImageIndex);
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
        return;

    vkResetFences(impl_->device, 1, &impl_->inFlightFence);
    impl_->imageAvailableConsumed = false;

    impl_->beginRenderPass(impl_->renderPassClear, clear);
    impl_->frameValid = true;
}

void Device::bindPipeline(PipelineHandle handle)
{
    if (!impl_->frameValid || handle == kInvalidHandle || handle > impl_->pipelines.size())
        return;
    const auto& pipeline = impl_->pipelines[handle - 1];
    if (!pipeline.pipeline)
        return;

    if (impl_->cached_pipeline == pipeline.pipeline)
        return;
    vkCmdBindPipeline(impl_->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);
    impl_->cached_pipeline = pipeline.pipeline;
}

void Device::bindVertexBuffer(BufferHandle handle, std::uint32_t )
{
    if (!impl_->frameValid || handle == kInvalidHandle || handle > impl_->buffers.size())
        return;
    VkBuffer buffer = impl_->buffers[handle - 1].buffer;
    if (!buffer)
        return;

    const VkDeviceSize offset = 0;
    if (impl_->cached_vertex_buffer == buffer && impl_->cached_vertex_offset == offset)
        return;
    vkCmdBindVertexBuffers(impl_->cmd, 0, 1, &buffer, &offset);
    impl_->cached_vertex_buffer = buffer;
    impl_->cached_vertex_offset = offset;
}

void Device::bindTexture(TextureHandle handle, std::uint32_t slot)
{
    if (!impl_->frameValid || slot != 0)
        return;

    VkDescriptorSet set = impl_->dummyDescriptorSet;
    if (handle != kInvalidHandle && handle <= impl_->textures.size() && impl_->textures[handle - 1].descriptorSet)
        set = impl_->textures[handle - 1].descriptorSet;

    if (impl_->cached_descriptor_set == set)
        return;
    vkCmdBindDescriptorSets(impl_->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, impl_->pipelineLayout, 0, 1, &set, 0,
                             nullptr);
    impl_->cached_descriptor_set = set;
}

void Device::draw(std::uint32_t vertexCount, std::uint32_t startVertex)
{
    if (!impl_->frameValid)
        return;
    vkCmdDraw(impl_->cmd, vertexCount, 1, startVertex, 0);
}

void Device::setViewport(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height)
{
    if (!impl_->frameValid || impl_->inShadowPass || impl_->width == 0 || impl_->height == 0)
        return;
    x = (std::min)(x, impl_->width - 1);
    y = (std::min)(y, impl_->height - 1);
    width  = (std::max)(1u, (std::min)(width, impl_->width - x));
    height = (std::max)(1u, (std::min)(height, impl_->height - y));

    VkViewport viewport{static_cast<float>(x),     static_cast<float>(y + height), static_cast<float>(width),
                         -static_cast<float>(height), 0.f,                            1.f};
    VkRect2D scissor{{static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)}, {width, height}};
    vkCmdSetViewport(impl_->cmd, 0, 1, &viewport);
    vkCmdSetScissor(impl_->cmd, 0, 1, &scissor);
}

void Device::resetViewport()
{
    setViewport(0, 0, impl_->width, impl_->height);
}

bool Device::beginShadowPass()
{
    if (!impl_->frameValid || impl_->inShadowPass || !impl_->renderPassShadow)
        return false;

    vkCmdEndRenderPass(impl_->cmd);

    VkClearValue clear{};
    clear.depthStencil = {1.f, 0};
    VkRenderPassBeginInfo rpInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpInfo.renderPass        = impl_->renderPassShadow;
    rpInfo.framebuffer       = impl_->shadowFramebuffer;
    rpInfo.renderArea.extent = {Impl::kShadowMapSize, Impl::kShadowMapSize};
    rpInfo.clearValueCount   = 1;
    rpInfo.pClearValues      = &clear;
    vkCmdBeginRenderPass(impl_->cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    const float size = static_cast<float>(Impl::kShadowMapSize);
    VkViewport viewport{0.f, size, size, -size, 0.f, 1.f};
    VkRect2D scissor{{0, 0}, {Impl::kShadowMapSize, Impl::kShadowMapSize}};
    vkCmdSetViewport(impl_->cmd, 0, 1, &viewport);
    vkCmdSetScissor(impl_->cmd, 0, 1, &scissor);

    impl_->resetBoundStateCache();
    impl_->inShadowPass = true;
    return true;
}

void Device::endShadowPass()
{
    if (!impl_->frameValid || !impl_->inShadowPass)
        return;

    vkCmdEndRenderPass(impl_->cmd);
    impl_->inShadowPass = false;

    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = impl_->swapchainImages[impl_->currentImageIndex];
    barrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.oldLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkMemoryBarrier memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    memory.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    memory.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    constexpr VkPipelineStageFlags kStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                             VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    vkCmdPipelineBarrier(impl_->cmd, kStages, kStages, 0, 1, &memory, 0, nullptr, 1, &barrier);

    impl_->recordMainPass(impl_->renderPassLoad, ClearColor{});
}

void Device::bindShadowMap()
{
    if (!impl_->frameValid || impl_->inShadowPass || impl_->shadowSetBound || !impl_->shadowDescriptorSet)
        return;
    vkCmdBindDescriptorSets(impl_->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, impl_->pipelineLayout, 1, 1,
                            &impl_->shadowDescriptorSet, 0, nullptr);
    impl_->shadowSetBound = true;
}

std::uint32_t Device::shadowMapSize() const
{
    return Impl::kShadowMapSize;
}

void Device::endFrame()
{
    if (!impl_->frameValid)
        return;

    vkCmdEndRenderPass(impl_->cmd);
    vkEndCommandBuffer(impl_->cmd);

    VkSemaphore& renderFinished = impl_->renderFinishedSemaphores[impl_->currentImageIndex];

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    if (!impl_->imageAvailableConsumed)
    {
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores    = &impl_->imageAvailable;
        submitInfo.pWaitDstStageMask  = &waitStage;
        impl_->imageAvailableConsumed = true;
    }
    submitInfo.commandBufferCount   = 1;
    submitInfo.pCommandBuffers      = &impl_->cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores    = &renderFinished;
    vkResetFences(impl_->device, 1, &impl_->inFlightFence);
    vkQueueSubmit(impl_->queue, 1, &submitInfo, impl_->inFlightFence);

    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = &renderFinished;
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &impl_->swapchain;
    presentInfo.pImageIndices      = &impl_->currentImageIndex;
    vkQueuePresentKHR(impl_->queue, &presentInfo);

    impl_->frameValid = false;
}

bool Device::readBackPixel(std::uint32_t x, std::uint32_t y, std::uint8_t outRgba[4])
{
    if (!impl_->frameValid || x >= impl_->width || y >= impl_->height)
        return false;

    vkCmdEndRenderPass(impl_->cmd);
    vkEndCommandBuffer(impl_->cmd);

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &impl_->cmd;
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    if (!impl_->imageAvailableConsumed)
    {
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores    = &impl_->imageAvailable;
        submitInfo.pWaitDstStageMask  = &waitStage;
        impl_->imageAvailableConsumed = true;
    }
    vkResetFences(impl_->device, 1, &impl_->inFlightFence);
    vkQueueSubmit(impl_->queue, 1, &submitInfo, impl_->inFlightFence);
    vkWaitForFences(impl_->device, 1, &impl_->inFlightFence, VK_TRUE, UINT64_MAX);

    const VkImage image         = impl_->swapchainImages[impl_->currentImageIndex];
    const VkDeviceSize bufSize  = static_cast<VkDeviceSize>(impl_->width) * impl_->height * 4;

    VkBuffer stagingBuffer       = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    const bool haveStaging       = impl_->createBuffer(
        bufSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingMemory);

    if (haveStaging)
    {
        impl_->runOneShotCommands([&](VkCommandBuffer cmd) {
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image               = image;
            barrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

            barrier.oldLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            barrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                  0, 0, nullptr, 0, nullptr, 1, &barrier);

            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent      = {impl_->width, impl_->height, 1};
            vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuffer, 1, &region);

            barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                  0, 0, nullptr, 0, nullptr, 1, &barrier);
        });

        void* mapped = nullptr;
        vkMapMemory(impl_->device, stagingMemory, 0, bufSize, 0, &mapped);
        const auto* row =
            static_cast<const std::uint8_t*>(mapped) + static_cast<std::size_t>(y) * impl_->width * 4;
        outRgba[0] = row[x * 4 + 0];
        outRgba[1] = row[x * 4 + 1];
        outRgba[2] = row[x * 4 + 2];
        outRgba[3] = row[x * 4 + 3];
        if (impl_->swapchainFormat == VK_FORMAT_B8G8R8A8_UNORM)
            std::swap(outRgba[0], outRgba[2]);
        vkUnmapMemory(impl_->device, stagingMemory);

        vkDestroyBuffer(impl_->device, stagingBuffer, nullptr);
        vkFreeMemory(impl_->device, stagingMemory, nullptr);
    }

    impl_->beginRenderPass(impl_->renderPassLoad, ClearColor{});
    return haveStaging;
}

void* Device::nativeDevice() const
{
    return impl_->device;
}

void* Device::nativeContext() const
{
    return impl_->cmd;
}

void* Device::nativeInstance() const
{
    return impl_->instance;
}

void* Device::nativePhysicalDevice() const
{
    return impl_->physicalDevice;
}

void* Device::nativeQueue() const
{
    return impl_->queue;
}

std::uint32_t Device::nativeQueueFamily() const
{
    return impl_->queueFamilyIndex;
}

void* Device::nativeRenderPass() const
{
    return impl_->renderPassClear;
}

std::uint32_t Device::swapchainImageCount() const
{
    return static_cast<std::uint32_t>(impl_->swapchainImages.size());
}

}

#else

namespace zio::render
{

struct Device::Impl
{
};

Device::Device() : impl_(std::make_unique<Impl>()) {}
Device::~Device() = default;

Device& Device::instance()
{
    static Device global;
    return global;
}

bool Device::init(void*, std::uint32_t, std::uint32_t)
{
    return false;
}

void Device::shutdown() {}

bool Device::isInitialized() const
{
    return false;
}

bool Device::resize(std::uint32_t, std::uint32_t)
{
    return false;
}

BufferHandle Device::createBuffer(const BufferDesc&)
{
    return kInvalidHandle;
}

void Device::destroyBuffer(BufferHandle) {}

bool Device::updateBuffer(BufferHandle, const void*, std::uint32_t)
{
    return false;
}

PipelineHandle Device::createPipeline(const PipelineDesc&)
{
    return kInvalidHandle;
}

void Device::destroyPipeline(PipelineHandle) {}

TextureHandle Device::createTexture(const TextureDesc&)
{
    return kInvalidHandle;
}

void Device::destroyTexture(TextureHandle) {}

void Device::beginFrame(const ClearColor&) {}
void Device::bindPipeline(PipelineHandle) {}
void Device::bindVertexBuffer(BufferHandle, std::uint32_t) {}
void Device::bindTexture(TextureHandle, std::uint32_t) {}
void Device::draw(std::uint32_t, std::uint32_t) {}
void Device::endFrame() {}

bool Device::readBackPixel(std::uint32_t, std::uint32_t, std::uint8_t[4])
{
    return false;
}

void* Device::nativeDevice() const
{
    return nullptr;
}

void* Device::nativeContext() const
{
    return nullptr;
}

void* Device::nativeInstance() const
{
    return nullptr;
}

void* Device::nativePhysicalDevice() const
{
    return nullptr;
}

void* Device::nativeQueue() const
{
    return nullptr;
}

std::uint32_t Device::nativeQueueFamily() const
{
    return 0;
}

void* Device::nativeRenderPass() const
{
    return nullptr;
}

std::uint32_t Device::swapchainImageCount() const
{
    return 0;
}

}

#endif
