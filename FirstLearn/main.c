#define WIN32_LEAN_AND_MEAN
#define VK_USE_PLATFORM_WIN32_KHR

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <vulkan/vulkan.h>

#pragma comment(lib, "vulkan-1.lib")

#define APP_WIDTH  800
#define APP_HEIGHT 600
#define MAX_SWAPCHAIN_IMAGES 8
#define MAX_FRAMES_IN_FLIGHT 2

#define VK_CHECK(x) do { \
    VkResult err = (x); \
    if (err != VK_SUCCESS) { \
        printf("Vulkan error: %d at %s:%d\n", err, __FILE__, __LINE__); \
        ExitProcess(1); \
    } \
} while (0)

typedef struct UniformBufferObject {
    float offset[4];
} UniformBufferObject;

typedef struct App {
    HINSTANCE hInstance;
    HWND hwnd;
    uint32_t width;
    uint32_t height;
    int running;

    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkSurfaceKHR surface;

    uint32_t graphicsQueueFamilyIndex;
    uint32_t presentQueueFamilyIndex;
    VkQueue graphicsQueue;
    VkQueue presentQueue;

    VkSwapchainKHR swapchain;
    VkFormat swapchainImageFormat;
    VkExtent2D swapchainExtent;
    uint32_t swapchainImageCount;
    VkImage swapchainImages[MAX_SWAPCHAIN_IMAGES];
    VkImageView swapchainImageViews[MAX_SWAPCHAIN_IMAGES];

    VkRenderPass renderPass;
    VkDescriptorSetLayout descriptorSetLayout;
    VkPipelineLayout pipelineLayout;
    VkPipeline graphicsPipeline;

    VkFramebuffer framebuffers[MAX_SWAPCHAIN_IMAGES];

    VkCommandPool commandPool;
    VkCommandBuffer commandBuffers[MAX_SWAPCHAIN_IMAGES];

    VkBuffer uniformBuffers[MAX_FRAMES_IN_FLIGHT];
    VkDeviceMemory uniformBufferMemories[MAX_FRAMES_IN_FLIGHT];
    void* uniformBufferMapped[MAX_FRAMES_IN_FLIGHT];

    VkDescriptorPool descriptorPool;
    VkDescriptorSet descriptorSets[MAX_FRAMES_IN_FLIGHT];

    VkSemaphore imageAvailableSemaphores[MAX_FRAMES_IN_FLIGHT];
    VkSemaphore renderFinishedSemaphores[MAX_FRAMES_IN_FLIGHT];
    VkFence inFlightFences[MAX_FRAMES_IN_FLIGHT];
    uint32_t currentFrame;
} App;

static App g_app = { 0 };

static void fatal(const char* msg)
{
    MessageBoxA(NULL, msg, "Error", MB_ICONERROR | MB_OK);
    printf("%s\n", msg);
    ExitProcess(1);
}

static char* readFileBinary(const char* path, size_t* outSize)
{
    FILE* fp = NULL;
    fopen_s(&fp, path, "rb");
    if (!fp) {
        printf("Failed to open file: %s\n", path);
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size <= 0) {
        fclose(fp);
        return NULL;
    }

    char* data = (char*)malloc((size_t)size);
    if (!data) {
        fclose(fp);
        return NULL;
    }

    fread(data, 1, (size_t)size, fp);
    fclose(fp);

    *outSize = (size_t)size;
    return data;
}

static VkShaderModule createShaderModule(VkDevice device, const char* path)
{
    size_t size = 0;
    char* code = readFileBinary(path, &size);
    if (!code) {
        char buf[512];
        sprintf_s(buf, sizeof(buf), "Shader file open failed: %s", path);
        fatal(buf);
    }

    VkShaderModuleCreateInfo ci = { 0 };
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = size;
    ci.pCode = (const uint32_t*)code;

    VkShaderModule module = VK_NULL_HANDLE;
    VK_CHECK(vkCreateShaderModule(device, &ci, NULL, &module));

    free(code);
    return module;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

static void createWindow(App* app)
{
    app->width = APP_WIDTH;
    app->height = APP_HEIGHT;

    WNDCLASSEXA wc = { 0 };
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = app->hInstance;
    wc.lpszClassName = "VulkanWindowClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    if (!RegisterClassExA(&wc)) {
        fatal("RegisterClassExA failed");
    }

    RECT rc = { 0, 0, (LONG)app->width, (LONG)app->height };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    app->hwnd = CreateWindowExA(
        0,
        wc.lpszClassName,
        "Vulkan Triangle - Uniform Buffer Animation",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rc.right - rc.left,
        rc.bottom - rc.top,
        NULL,
        NULL,
        app->hInstance,
        NULL
    );

    if (!app->hwnd) {
        fatal("CreateWindowExA failed");
    }

    printf("Window created.\n");
}

static void createInstance(App* app)
{
    const char* extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME
    };

    VkApplicationInfo ai = { 0 };
    ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName = "Vulkan Triangle";
    ai.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    ai.pEngineName = "No Engine";
    ai.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    ai.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo ci = { 0 };
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &ai;
    ci.enabledExtensionCount = (uint32_t)(sizeof(extensions) / sizeof(extensions[0]));
    ci.ppEnabledExtensionNames = extensions;

    VK_CHECK(vkCreateInstance(&ci, NULL, &app->instance));
    printf("vkCreateInstance succeeded!\n");
}

static void createSurface(App* app)
{
    VkWin32SurfaceCreateInfoKHR ci = { 0 };
    ci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    ci.hinstance = app->hInstance;
    ci.hwnd = app->hwnd;

    VK_CHECK(vkCreateWin32SurfaceKHR(app->instance, &ci, NULL, &app->surface));
    printf("vkCreateWin32SurfaceKHR succeeded!\n");
}

static int checkDeviceExtensionSupport(VkPhysicalDevice device)
{
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, NULL, &count, NULL);

    VkExtensionProperties* props = (VkExtensionProperties*)malloc(sizeof(VkExtensionProperties) * count);
    vkEnumerateDeviceExtensionProperties(device, NULL, &count, props);

    int foundSwapchain = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (strcmp(props[i].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
            foundSwapchain = 1;
            break;
        }
    }

    free(props);
    return foundSwapchain;
}

static void pickPhysicalDevice(App* app)
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(app->instance, &deviceCount, NULL);
    if (deviceCount == 0) {
        fatal("No Vulkan physical devices found");
    }

    VkPhysicalDevice* devices = (VkPhysicalDevice*)malloc(sizeof(VkPhysicalDevice) * deviceCount);
    vkEnumeratePhysicalDevices(app->instance, &deviceCount, devices);

    app->physicalDevice = VK_NULL_HANDLE;

    for (uint32_t i = 0; i < deviceCount; ++i) {
        VkPhysicalDevice device = devices[i];

        if (!checkDeviceExtensionSupport(device)) {
            continue;
        }

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, NULL);

        VkQueueFamilyProperties* queueFamilies =
            (VkQueueFamilyProperties*)malloc(sizeof(VkQueueFamilyProperties) * queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies);

        int foundGraphics = 0;
        int foundPresent = 0;
        uint32_t graphicsIndex = 0;
        uint32_t presentIndex = 0;

        for (uint32_t q = 0; q < queueFamilyCount; ++q) {
            if (queueFamilies[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                graphicsIndex = q;
                foundGraphics = 1;
            }

            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, q, app->surface, &presentSupport);
            if (presentSupport) {
                presentIndex = q;
                foundPresent = 1;
            }

            if (foundGraphics && foundPresent) {
                break;
            }
        }

        free(queueFamilies);

        if (foundGraphics && foundPresent) {
            app->physicalDevice = device;
            app->graphicsQueueFamilyIndex = graphicsIndex;
            app->presentQueueFamilyIndex = presentIndex;

            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(device, &props);
            printf("Selected GPU: %s\n", props.deviceName);
            break;
        }
    }

    free(devices);

    if (app->physicalDevice == VK_NULL_HANDLE) {
        fatal("No suitable physical device found");
    }

    printf("Graphics Queue Family Index = %u\n", app->graphicsQueueFamilyIndex);
    printf("Present  Queue Family Index = %u\n", app->presentQueueFamilyIndex);
}

static void createDevice(App* app)
{
    float priority = 1.0f;

    VkDeviceQueueCreateInfo queueInfos[2];
    uint32_t queueInfoCount = 0;

    if (app->graphicsQueueFamilyIndex == app->presentQueueFamilyIndex) {
        queueInfos[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfos[0].pNext = NULL;
        queueInfos[0].flags = 0;
        queueInfos[0].queueFamilyIndex = app->graphicsQueueFamilyIndex;
        queueInfos[0].queueCount = 1;
        queueInfos[0].pQueuePriorities = &priority;
        queueInfoCount = 1;
    }
    else {
        queueInfos[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfos[0].pNext = NULL;
        queueInfos[0].flags = 0;
        queueInfos[0].queueFamilyIndex = app->graphicsQueueFamilyIndex;
        queueInfos[0].queueCount = 1;
        queueInfos[0].pQueuePriorities = &priority;

        queueInfos[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfos[1].pNext = NULL;
        queueInfos[1].flags = 0;
        queueInfos[1].queueFamilyIndex = app->presentQueueFamilyIndex;
        queueInfos[1].queueCount = 1;
        queueInfos[1].pQueuePriorities = &priority;

        queueInfoCount = 2;
    }

    const char* extensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    VkPhysicalDeviceFeatures features = { 0 };

    VkDeviceCreateInfo ci = { 0 };
    ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount = queueInfoCount;
    ci.pQueueCreateInfos = queueInfos;
    ci.enabledExtensionCount = 1;
    ci.ppEnabledExtensionNames = extensions;
    ci.pEnabledFeatures = &features;

    VK_CHECK(vkCreateDevice(app->physicalDevice, &ci, NULL, &app->device));
    printf("vkCreateDevice succeeded!\n");

    vkGetDeviceQueue(app->device, app->graphicsQueueFamilyIndex, 0, &app->graphicsQueue);
    vkGetDeviceQueue(app->device, app->presentQueueFamilyIndex, 0, &app->presentQueue);
    printf("Queues acquired.\n");
}

static VkSurfaceFormatKHR chooseSurfaceFormat(const VkSurfaceFormatKHR* formats, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return formats[i];
        }
    }
    return formats[0];
}

static VkPresentModeKHR choosePresentMode(const VkPresentModeKHR* modes, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
            return modes[i];
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

static VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR* caps, uint32_t width, uint32_t height)
{
    if (caps->currentExtent.width != UINT32_MAX) {
        return caps->currentExtent;
    }

    VkExtent2D extent;
    extent.width = width;
    extent.height = height;

    if (extent.width < caps->minImageExtent.width) extent.width = caps->minImageExtent.width;
    if (extent.width > caps->maxImageExtent.width) extent.width = caps->maxImageExtent.width;
    if (extent.height < caps->minImageExtent.height) extent.height = caps->minImageExtent.height;
    if (extent.height > caps->maxImageExtent.height) extent.height = caps->maxImageExtent.height;

    return extent;
}

static void createSwapchain(App* app)
{
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(app->physicalDevice, app->surface, &caps);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(app->physicalDevice, app->surface, &formatCount, NULL);
    VkSurfaceFormatKHR* formats = (VkSurfaceFormatKHR*)malloc(sizeof(VkSurfaceFormatKHR) * formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(app->physicalDevice, app->surface, &formatCount, formats);

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(app->physicalDevice, app->surface, &presentModeCount, NULL);
    VkPresentModeKHR* presentModes = (VkPresentModeKHR*)malloc(sizeof(VkPresentModeKHR) * presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(app->physicalDevice, app->surface, &presentModeCount, presentModes);

    VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(formats, formatCount);
    VkPresentModeKHR presentMode = choosePresentMode(presentModes, presentModeCount);
    VkExtent2D extent = chooseExtent(&caps, app->width, app->height);

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) {
        imageCount = caps.maxImageCount;
    }
    if (imageCount > MAX_SWAPCHAIN_IMAGES) {
        fatal("Increase MAX_SWAPCHAIN_IMAGES");
    }

    VkSwapchainCreateInfoKHR ci = { 0 };
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = app->surface;
    ci.minImageCount = imageCount;
    ci.imageFormat = surfaceFormat.format;
    ci.imageColorSpace = surfaceFormat.colorSpace;
    ci.imageExtent = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    uint32_t queueFamilyIndices[] = {
        app->graphicsQueueFamilyIndex,
        app->presentQueueFamilyIndex
    };

    if (app->graphicsQueueFamilyIndex != app->presentQueueFamilyIndex) {
        ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices = queueFamilyIndices;
    }
    else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = presentMode;
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = VK_NULL_HANDLE;

    VK_CHECK(vkCreateSwapchainKHR(app->device, &ci, NULL, &app->swapchain));
    printf("vkCreateSwapchainKHR succeeded!\n");

    vkGetSwapchainImagesKHR(app->device, app->swapchain, &imageCount, NULL);
    app->swapchainImageCount = imageCount;
    vkGetSwapchainImagesKHR(app->device, app->swapchain, &imageCount, app->swapchainImages);

    app->swapchainImageFormat = surfaceFormat.format;
    app->swapchainExtent = extent;

    printf("Swapchain image count = %u\n", app->swapchainImageCount);

    free(formats);
    free(presentModes);
}

static void createImageViews(App* app)
{
    for (uint32_t i = 0; i < app->swapchainImageCount; ++i) {
        VkImageViewCreateInfo ci = { 0 };
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ci.image = app->swapchainImages[i];
        ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ci.format = app->swapchainImageFormat;
        ci.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        ci.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        ci.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        ci.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ci.subresourceRange.baseMipLevel = 0;
        ci.subresourceRange.levelCount = 1;
        ci.subresourceRange.baseArrayLayer = 0;
        ci.subresourceRange.layerCount = 1;

        VK_CHECK(vkCreateImageView(app->device, &ci, NULL, &app->swapchainImageViews[i]));
        printf("ImageView %u created.\n", i);
    }
}

static void createRenderPass(App* app)
{
    VkAttachmentDescription colorAttachment = { 0 };
    colorAttachment.format = app->swapchainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef = { 0 };
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = { 0 };
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency = { 0 };
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo ci = { 0 };
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 1;
    ci.pAttachments = &colorAttachment;
    ci.subpassCount = 1;
    ci.pSubpasses = &subpass;
    ci.dependencyCount = 1;
    ci.pDependencies = &dependency;

    VK_CHECK(vkCreateRenderPass(app->device, &ci, NULL, &app->renderPass));
    printf("vkCreateRenderPass succeeded!\n");
}

static uint32_t findMemoryType(App* app, uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(app->physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    fatal("Suitable memory type not found");
    return 0;
}

static void createBuffer(
    App* app,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkBuffer* outBuffer,
    VkDeviceMemory* outMemory)
{
    VkBufferCreateInfo bufferInfo = { 0 };
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VK_CHECK(vkCreateBuffer(app->device, &bufferInfo, NULL, outBuffer));

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(app->device, *outBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo = { 0 };
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(app, memRequirements.memoryTypeBits, properties);

    VK_CHECK(vkAllocateMemory(app->device, &allocInfo, NULL, outMemory));
    VK_CHECK(vkBindBufferMemory(app->device, *outBuffer, *outMemory, 0));
}

static void createDescriptorSetLayout(App* app)
{
    VkDescriptorSetLayoutBinding uboLayoutBinding = { 0 };
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    uboLayoutBinding.pImmutableSamplers = NULL;

    VkDescriptorSetLayoutCreateInfo layoutInfo = { 0 };
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboLayoutBinding;

    VK_CHECK(vkCreateDescriptorSetLayout(app->device, &layoutInfo, NULL, &app->descriptorSetLayout));
    printf("vkCreateDescriptorSetLayout succeeded!\n");
}

static void createGraphicsPipeline(App* app)
{
    VkShaderModule vertModule = createShaderModule(app->device, "triangle.vert.spv");
    VkShaderModule fragModule = createShaderModule(app->device, "triangle.frag.spv");

    VkPipelineShaderStageCreateInfo vertStage = { 0 };
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertModule;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage = { 0 };
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragModule;
    fragStage.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertStage, fragStage };

    VkPipelineVertexInputStateCreateInfo vertexInput = { 0 };
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = { 0 };
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport = { 0 };
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)app->swapchainExtent.width;
    viewport.height = (float)app->swapchainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = { 0 };
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent = app->swapchainExtent;

    VkPipelineViewportStateCreateInfo viewportState = { 0 };
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer = { 0 };
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling = { 0 };
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment = { 0 };
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending = { 0 };
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = { 0 };
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &app->descriptorSetLayout;

    VK_CHECK(vkCreatePipelineLayout(app->device, &pipelineLayoutInfo, NULL, &app->pipelineLayout));

    VkGraphicsPipelineCreateInfo pipelineInfo = { 0 };
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = NULL;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = NULL;
    pipelineInfo.layout = app->pipelineLayout;
    pipelineInfo.renderPass = app->renderPass;
    pipelineInfo.subpass = 0;

    VK_CHECK(vkCreateGraphicsPipelines(app->device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &app->graphicsPipeline));
    printf("vkCreateGraphicsPipelines succeeded!\n");

    vkDestroyShaderModule(app->device, fragModule, NULL);
    vkDestroyShaderModule(app->device, vertModule, NULL);
}

static void createFramebuffers(App* app)
{
    for (uint32_t i = 0; i < app->swapchainImageCount; ++i) {
        VkImageView attachments[] = {
            app->swapchainImageViews[i]
        };

        VkFramebufferCreateInfo ci = { 0 };
        ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        ci.renderPass = app->renderPass;
        ci.attachmentCount = 1;
        ci.pAttachments = attachments;
        ci.width = app->swapchainExtent.width;
        ci.height = app->swapchainExtent.height;
        ci.layers = 1;

        VK_CHECK(vkCreateFramebuffer(app->device, &ci, NULL, &app->framebuffers[i]));
        printf("Framebuffer %u created.\n", i);
    }
}

static void createCommandPool(App* app)
{
    VkCommandPoolCreateInfo ci = { 0 };
    ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ci.queueFamilyIndex = app->graphicsQueueFamilyIndex;

    VK_CHECK(vkCreateCommandPool(app->device, &ci, NULL, &app->commandPool));
    printf("vkCreateCommandPool succeeded!\n");
}

static void createCommandBuffers(App* app)
{
    VkCommandBufferAllocateInfo ai = { 0 };
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = app->commandPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = app->swapchainImageCount;

    VK_CHECK(vkAllocateCommandBuffers(app->device, &ai, app->commandBuffers));
    printf("CommandBuffers allocated.\n");
}

static void createUniformBuffers(App* app)
{
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        createBuffer(
            app,
            bufferSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &app->uniformBuffers[i],
            &app->uniformBufferMemories[i]);

        VK_CHECK(vkMapMemory(app->device, app->uniformBufferMemories[i], 0, bufferSize, 0, &app->uniformBufferMapped[i]));
    }

    printf("UniformBuffers created and mapped.\n");
}

static void createDescriptorPool(App* app)
{
    VkDescriptorPoolSize poolSize = { 0 };
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = MAX_FRAMES_IN_FLIGHT;

    VkDescriptorPoolCreateInfo poolInfo = { 0 };
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = MAX_FRAMES_IN_FLIGHT;

    VK_CHECK(vkCreateDescriptorPool(app->device, &poolInfo, NULL, &app->descriptorPool));
    printf("vkCreateDescriptorPool succeeded!\n");
}

static void createDescriptorSets(App* app)
{
    VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT];
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        layouts[i] = app->descriptorSetLayout;
    }

    VkDescriptorSetAllocateInfo allocInfo = { 0 };
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = app->descriptorPool;
    allocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    allocInfo.pSetLayouts = layouts;

    VK_CHECK(vkAllocateDescriptorSets(app->device, &allocInfo, app->descriptorSets));

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkDescriptorBufferInfo bufferInfo = { 0 };
        bufferInfo.buffer = app->uniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        VkWriteDescriptorSet descriptorWrite = { 0 };
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = app->descriptorSets[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(app->device, 1, &descriptorWrite, 0, NULL);
    }

    printf("DescriptorSets created.\n");
}

static float getTimeSec(void)
{
    static LARGE_INTEGER freq = { 0 };
    static LARGE_INTEGER start = { 0 };

    LARGE_INTEGER now;

    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start);
    }

    QueryPerformanceCounter(&now);
    return (float)((double)(now.QuadPart - start.QuadPart) / (double)freq.QuadPart);
}

static void updateUniformBuffer(App* app, uint32_t frameIndex)
{
    UniformBufferObject ubo;
    float t = getTimeSec();

    ubo.offset[0] = sinf(t) * 0.5f;
    ubo.offset[1] = 0.0f;
    ubo.offset[2] = 0.0f;
    ubo.offset[3] = 0.0f;

    memcpy(app->uniformBufferMapped[frameIndex], &ubo, sizeof(ubo));
}

static void recordCommandBuffer(App* app, uint32_t imageIndex, uint32_t frameIndex)
{
    VkCommandBuffer cmd = app->commandBuffers[imageIndex];

    VK_CHECK(vkResetCommandBuffer(cmd, 0));

    VkCommandBufferBeginInfo bi = { 0 };
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    VK_CHECK(vkBeginCommandBuffer(cmd, &bi));

    VkClearValue clearColor;
    clearColor.color.float32[0] = 0.0f;
    clearColor.color.float32[1] = 0.0f;
    clearColor.color.float32[2] = 0.2f;
    clearColor.color.float32[3] = 1.0f;

    VkRenderPassBeginInfo rpbi = { 0 };
    rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass = app->renderPass;
    rpbi.framebuffer = app->framebuffers[imageIndex];
    rpbi.renderArea.offset.x = 0;
    rpbi.renderArea.offset.y = 0;
    rpbi.renderArea.extent = app->swapchainExtent;
    rpbi.clearValueCount = 1;
    rpbi.pClearValues = &clearColor;

    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        app->pipelineLayout,
        0,
        1,
        &app->descriptorSets[frameIndex],
        0,
        NULL);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    VK_CHECK(vkEndCommandBuffer(cmd));
}

static void createSyncObjects(App* app)
{
    VkSemaphoreCreateInfo sci = { 0 };
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fci = { 0 };
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VK_CHECK(vkCreateSemaphore(app->device, &sci, NULL, &app->imageAvailableSemaphores[i]));
        VK_CHECK(vkCreateSemaphore(app->device, &sci, NULL, &app->renderFinishedSemaphores[i]));
        VK_CHECK(vkCreateFence(app->device, &fci, NULL, &app->inFlightFences[i]));
    }

    app->currentFrame = 0;
    printf("Sync objects created.\n");
}

static void drawFrame(App* app)
{
    vkWaitForFences(app->device, 1, &app->inFlightFences[app->currentFrame], VK_TRUE, UINT64_MAX);
    vkResetFences(app->device, 1, &app->inFlightFences[app->currentFrame]);

    uint32_t imageIndex = 0;
    VkResult res = vkAcquireNextImageKHR(
        app->device,
        app->swapchain,
        UINT64_MAX,
        app->imageAvailableSemaphores[app->currentFrame],
        VK_NULL_HANDLE,
        &imageIndex
    );

    if (res == VK_ERROR_OUT_OF_DATE_KHR) {
        printf("Swapchain out of date (resize handling not implemented yet).\n");
        return;
    }
    else if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) {
        VK_CHECK(res);
    }

    updateUniformBuffer(app, app->currentFrame);
    recordCommandBuffer(app, imageIndex, app->currentFrame);

    VkSemaphore waitSemaphores[] = {
        app->imageAvailableSemaphores[app->currentFrame]
    };
    VkPipelineStageFlags waitStages[] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
    };
    VkSemaphore signalSemaphores[] = {
        app->renderFinishedSemaphores[app->currentFrame]
    };

    VkSubmitInfo submitInfo = { 0 };
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &app->commandBuffers[imageIndex];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    VK_CHECK(vkQueueSubmit(app->graphicsQueue, 1, &submitInfo, app->inFlightFences[app->currentFrame]));

    VkSwapchainKHR swapchains[] = { app->swapchain };

    VkPresentInfoKHR presentInfo = { 0 };
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    res = vkQueuePresentKHR(app->presentQueue, &presentInfo);
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR) {
        printf("Present: swapchain needs recreation (not implemented yet).\n");
    }
    else if (res != VK_SUCCESS) {
        VK_CHECK(res);
    }

    app->currentFrame = (app->currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

static void mainLoop(App* app)
{
    MSG msg;
    app->running = 1;

    while (app->running) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                app->running = 0;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (!app->running) {
            break;
        }

        drawFrame(app);
    }

    vkDeviceWaitIdle(app->device);
}

static void cleanup(App* app)
{
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (app->uniformBufferMapped[i]) {
            vkUnmapMemory(app->device, app->uniformBufferMemories[i]);
            app->uniformBufferMapped[i] = NULL;
        }
        if (app->uniformBuffers[i]) {
            vkDestroyBuffer(app->device, app->uniformBuffers[i], NULL);
        }
        if (app->uniformBufferMemories[i]) {
            vkFreeMemory(app->device, app->uniformBufferMemories[i], NULL);
        }
    }

    if (app->descriptorPool) {
        vkDestroyDescriptorPool(app->device, app->descriptorPool, NULL);
    }

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (app->inFlightFences[i]) {
            vkDestroyFence(app->device, app->inFlightFences[i], NULL);
        }
        if (app->renderFinishedSemaphores[i]) {
            vkDestroySemaphore(app->device, app->renderFinishedSemaphores[i], NULL);
        }
        if (app->imageAvailableSemaphores[i]) {
            vkDestroySemaphore(app->device, app->imageAvailableSemaphores[i], NULL);
        }
    }

    if (app->commandPool) {
        vkDestroyCommandPool(app->device, app->commandPool, NULL);
    }

    for (uint32_t i = 0; i < app->swapchainImageCount; ++i) {
        if (app->framebuffers[i]) {
            vkDestroyFramebuffer(app->device, app->framebuffers[i], NULL);
        }
    }

    if (app->graphicsPipeline) {
        vkDestroyPipeline(app->device, app->graphicsPipeline, NULL);
    }

    if (app->pipelineLayout) {
        vkDestroyPipelineLayout(app->device, app->pipelineLayout, NULL);
    }

    if (app->descriptorSetLayout) {
        vkDestroyDescriptorSetLayout(app->device, app->descriptorSetLayout, NULL);
    }

    if (app->renderPass) {
        vkDestroyRenderPass(app->device, app->renderPass, NULL);
    }

    for (uint32_t i = 0; i < app->swapchainImageCount; ++i) {
        if (app->swapchainImageViews[i]) {
            vkDestroyImageView(app->device, app->swapchainImageViews[i], NULL);
        }
    }

    if (app->swapchain) {
        vkDestroySwapchainKHR(app->device, app->swapchain, NULL);
    }

    if (app->device) {
        vkDestroyDevice(app->device, NULL);
    }

    if (app->surface) {
        vkDestroySurfaceKHR(app->instance, app->surface, NULL);
    }

    if (app->instance) {
        vkDestroyInstance(app->instance, NULL);
    }
}

int main(void)
{
    App* app = &g_app;
    memset(app, 0, sizeof(*app));

    app->hInstance = GetModuleHandleA(NULL);

    createWindow(app);
    createInstance(app);
    createSurface(app);
    pickPhysicalDevice(app);
    createDevice(app);
    createSwapchain(app);
    createImageViews(app);
    createRenderPass(app);
    createDescriptorSetLayout(app);
    createGraphicsPipeline(app);
    createFramebuffers(app);
    createCommandPool(app);
    createCommandBuffers(app);
    createUniformBuffers(app);
    createDescriptorPool(app);
    createDescriptorSets(app);
    createSyncObjects(app);

    mainLoop(app);
    cleanup(app);

    return 0;
}

