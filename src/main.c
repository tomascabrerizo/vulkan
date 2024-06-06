#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_vulkan.h>
#include <vulkan/vulkan.h>

#define array_len(v) (sizeof((v)) / sizeof((v)[0]))
#define is_power_of_two(expr) (((expr) & (expr - 1)) == 0)
#define unused(v) ((void)(v))

#define kb(x) ((x) * 1024ll)
#define mb(x) (kb(x) * 1024ll)
#define gb(x) (mb(x) * 1024ll)

#define clamp(a, b, c) max(min(a, c), b)

typedef union V2 {
    struct {
        float x, y;
    };
    float m[2];
} V2;

inline V2 v2(float x, float y) {
    return (V2){ x, y };
}

typedef union V3 {
    struct {
        float x, y, z;
    };
    struct {
        float r, g, b;
    };
    float m[3];
} V3;

inline V3 v3(float x, float y, float z) {
    return (V3){ x, y, z };
}

typedef struct Vertex {
    V2 pos;
    V3 color;
} Vertex;

#define VERTEX_LOC_POS 0
#define VERTEX_LOC_COL 1

typedef struct Arena {
    void *data;
    size_t used;
    size_t size;
} Arena;

Arena arena_create(size_t size) {
    Arena arena;
    arena.data = malloc(size);
    arena.used = 0;
    arena.size = size;
    return arena;
}

void *arena_push(Arena *arena, size_t size, size_t align) {
    assert(is_power_of_two(align));
    uintptr_t unaligned_addr = (uintptr_t)arena->data + arena->used;
    uintptr_t aligned_addr   = (unaligned_addr + (align - 1)) & ~(align - 1);
    assert(aligned_addr % align == 0);
    size_t current_used = (aligned_addr - (uintptr_t)arena->data);
    size_t total_used   = current_used + size;
    assert(total_used <= arena->size);
    arena->used  = total_used;
    void *result = (void *)aligned_addr;
    memset(result, 0, size);
    return result;
}

void arena_clear(Arena *arena) {
    arena->used = 0;
}

typedef struct File {
    void *data;
    size_t size;
} File;

File read_entire_file(Arena *arena, const char *path) {
    File result = { 0 };
    FILE *file  = fopen(path, "rb");
    fseek(file, 0, SEEK_END);
    unsigned int size = ftell(file);
    fseek(file, 0, SEEK_SET);
    result.data = arena_push(arena, size + 1, 1);
    result.size = size;
    fread(result.data, result.size, 1, file);
    ((unsigned char *)result.data)[result.size] = '\0';
    fclose(file);
    return result;
}

#define MAX_FRAMES_IN_FLIGHT 2
const char *validation_layers[] = { "VK_LAYER_KHRONOS_validation" };
const char *device_extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

const Vertex vertices[] = {
    {{ 0.0f, -0.5f }, { 1.0f, 0.0f, 0.0f }},
    { { 0.5f, 0.5f }, { 0.0f, 1.0f, 0.0f }},
    {{ -0.5f, 0.5f }, { 0.0f, 0.0f, 1.0f }}
};

static inline VkVertexInputBindingDescription vertex_get_binding_description(void) {
    VkVertexInputBindingDescription bindingDescription = { 0 };
    bindingDescription.binding                         = 0;
    bindingDescription.stride                          = sizeof(Vertex);
    bindingDescription.inputRate                       = VK_VERTEX_INPUT_RATE_VERTEX;
    return bindingDescription;
}

static inline void vertex_get_attribute_desc(VkVertexInputAttributeDescription *attr_desc) {
    attr_desc[VERTEX_LOC_POS].binding  = 0;
    attr_desc[VERTEX_LOC_POS].location = 0;
    attr_desc[VERTEX_LOC_POS].format   = VK_FORMAT_R32G32_SFLOAT;
    attr_desc[VERTEX_LOC_POS].offset   = offsetof(Vertex, pos);

    attr_desc[VERTEX_LOC_COL].binding  = 0;
    attr_desc[VERTEX_LOC_COL].location = 1;
    attr_desc[VERTEX_LOC_COL].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attr_desc[VERTEX_LOC_COL].offset   = offsetof(Vertex, color);
}

typedef struct VkState {

    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical_device;
    unsigned int present_queue_index, graphics_queue_index, queue_family_count;
    VkDevice device;

    VkFormat swapchain_image_format;
    VkExtent2D swapchain_extent;
    VkSwapchainKHR swapchain;

    unsigned int swapchain_images_count;
    VkImageView *swapchain_images_views;

    VkRenderPass render_pass;
    VkPipeline pipeline;

    VkFramebuffer *framebuffers;
    unsigned int framebuffers_count;

    VkCommandPool command_pool;
    VkCommandBuffer command_buffers[MAX_FRAMES_IN_FLIGHT];

    VkSemaphore image_available_semaphores[MAX_FRAMES_IN_FLIGHT];
    VkSemaphore render_finished_semaphores[MAX_FRAMES_IN_FLIGHT];
    VkFence in_flight_fences[MAX_FRAMES_IN_FLIGHT];

    unsigned int current_frame;
    bool framebuffer_resized;

    VkBuffer vertex_buffer;
    VkDeviceMemory vertex_buffer_memory;

} VkState;

void check_device_extensions(VkPhysicalDevice device, Arena *arena, const char **extensions,
                             unsigned extensions_count, bool *extensions_found) {
    *extensions_found = true;

    unsigned int device_extension_count = 0;
    vkEnumerateDeviceExtensionProperties(device, NULL, &device_extension_count, NULL);
    VkExtensionProperties *device_extension_props = (VkExtensionProperties *)arena_push(
        arena, sizeof(VkExtensionProperties) * device_extension_count, 1);
    vkEnumerateDeviceExtensionProperties(device, NULL, &device_extension_count,
                                         device_extension_props);

    for(unsigned int required_extension_index = 0; required_extension_index < extensions_count;
        ++required_extension_index) {
        bool found = false;
        for(unsigned int extension_index = 0; extension_index < device_extension_count;
            ++extension_index) {
            if(strcmp(extensions[required_extension_index],
                      device_extension_props[extension_index].extensionName) == 0) {
                found = true;
                break;
            }
        }

        if(!found) {
            *extensions_found = false;
            break;
        }
    }
}

void check_validation_layers(Arena *arena, const char **validation_layers,
                             unsigned int validtion_layers_count, bool *validation_layers_found) {
    *validation_layers_found = true;

    unsigned int layers_count = 0;
    vkEnumerateInstanceLayerProperties(&layers_count, NULL);
    VkLayerProperties *layers_props =
        (VkLayerProperties *)arena_push(arena, sizeof(VkLayerProperties) * layers_count, 1);
    vkEnumerateInstanceLayerProperties(&layers_count, layers_props);
    for(unsigned int validation_layer_index = 0; validation_layer_index < validtion_layers_count;
        ++validation_layer_index) {

        bool layer_found = false;

        for(unsigned int layer_index = 0; layer_index < layers_count; ++layer_index) {
            const char *validation_layer = validation_layers[validation_layer_index];
            if(strcmp(validation_layer, layers_props[layer_index].layerName) == 0) {
                layer_found = true;
                break;
            }
        }

        if(!layer_found) {
            *validation_layers_found = false;
            break;
        }
    }
}

void vulkan_create_instance(VkState *state, Arena *arena, SDL_Window *window) {
    // Create vulkan instance
    VkApplicationInfo app_info  = { 0 };
    app_info.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pNext              = NULL;
    app_info.pApplicationName   = "vulkan";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName        = NULL;
    app_info.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion         = VK_API_VERSION_1_0;

    // NOTE: Get SDL2 extensions
    unsigned int instance_extensions_count = 0;
    SDL_Vulkan_GetInstanceExtensions(window, &instance_extensions_count, NULL);
    const char **instance_extensions_names =
        (const char **)arena_push(arena, instance_extensions_count * sizeof(const char **), 1);
    SDL_Vulkan_GetInstanceExtensions(window, &instance_extensions_count, instance_extensions_names);

    // NOTE: Setup Validation layers
    bool validation_layer_found = false;
    check_validation_layers(arena, validation_layers, array_len(validation_layers),
                            &validation_layer_found);

    VkInstanceCreateInfo create_info    = { 0 };
    create_info.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo        = &app_info;
    create_info.enabledExtensionCount   = instance_extensions_count;
    create_info.ppEnabledExtensionNames = instance_extensions_names;
    create_info.enabledLayerCount       = validation_layer_found ? array_len(validation_layers) : 0;
    create_info.ppEnabledLayerNames     = validation_layers;

    if(vkCreateInstance(&create_info, NULL, &state->instance) != VK_SUCCESS) {
        printf("Fail to create VkInstace\n");
        exit(1);
    }
}

void vulkan_create_surface(VkState *state, SDL_Window *window) {
    // Create Window Surface
    if(SDL_Vulkan_CreateSurface(window, state->instance, &state->surface) == SDL_FALSE) {
        printf("Fail to create Vulkan surface\n");
        exit(1);
    }
}

void vulkan_select_physical_device(VkState *state, Arena *arena) {
    // Selecting a physical device
    unsigned int device_count = 0;
    vkEnumeratePhysicalDevices(state->instance, &device_count, NULL);
    if(device_count == 0) {
        printf("Fail to find GPU with vulkan support\n");
        exit(1);
    }
    VkPhysicalDevice *physical_devices =
        (VkPhysicalDevice *)arena_push(arena, sizeof(VkPhysicalDevice) * device_count, 1);
    vkEnumeratePhysicalDevices(state->instance, &device_count, physical_devices);

    // NOTE: Find suitable device
    state->physical_device = VK_NULL_HANDLE;

    for(unsigned int device_index = 0; device_index < device_count; ++device_index) {

        VkPhysicalDevice device = physical_devices[device_index];
        VkPhysicalDeviceProperties device_props;
        VkPhysicalDeviceFeatures device_feats;
        vkGetPhysicalDeviceProperties(device, &device_props);
        vkGetPhysicalDeviceFeatures(device, &device_feats);

        if(device_props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
           device_feats.geometryShader) {
            state->physical_device = device;
            break;
        }
    }

    assert(state->physical_device != VK_NULL_HANDLE);
}

void vulkan_find_family_queues(VkState *state, Arena *arena) {
    // NOTE: Find queue family queues
    state->queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(state->physical_device, &state->queue_family_count,
                                             NULL);
    VkQueueFamilyProperties *queue_family_props =
        arena_push(arena, sizeof(VkQueueFamilyProperties) * state->queue_family_count, 1);
    vkGetPhysicalDeviceQueueFamilyProperties(state->physical_device, &state->queue_family_count,
                                             queue_family_props);
    state->graphics_queue_index = (unsigned int)-1;
    state->present_queue_index  = (unsigned int)-1;

    for(unsigned int i = 0; i < state->queue_family_count; ++i) {

        VkBool32 present_support = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(state->physical_device, i, state->surface,
                                             &present_support);
        if(present_support) {
            state->present_queue_index = i;
        }

        if(queue_family_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            state->graphics_queue_index = i;
        }

        if(state->present_queue_index != (unsigned int)-1 &&
           state->graphics_queue_index != (unsigned int)-1) {
            break;
        }
    }

    if(state->graphics_queue_index == (unsigned int)-1 ||
       state->present_queue_index == (unsigned int)-1) {
        printf("Present Queue not supported!\n");
        exit(1);
    }
}

void vulkan_create_logical_device(VkState *state, Arena *arena) {
    float queue_priority = 1.0f;

    unsigned int queue_families[] = { state->present_queue_index, state->graphics_queue_index };
    VkDeviceQueueCreateInfo *queue_create_infos = (VkDeviceQueueCreateInfo *)arena_push(
        arena, sizeof(VkDeviceQueueCreateInfo) * array_len(queue_families), 1);
    unsigned int unique_families_count = 0;
    bool *unique_families_state = arena_push(arena, sizeof(bool) * state->queue_family_count, 1);
    memset(unique_families_state, 0, sizeof(bool) * state->queue_family_count);

    for(unsigned int queue_index = 0; queue_index < array_len(queue_families); ++queue_index) {
        if(unique_families_state[queue_families[queue_index]] == false) {

            VkDeviceQueueCreateInfo queue_create_info = { 0 };
            queue_create_info.sType                   = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queue_create_info.queueFamilyIndex        = queue_families[queue_index];
            queue_create_info.queueCount              = 1;
            queue_create_info.pQueuePriorities        = &queue_priority;

            queue_create_infos[unique_families_count++]        = queue_create_info;
            unique_families_state[queue_families[queue_index]] = true;
        }
    }

    // Enable device extensions
    bool device_extensions_found = true;
    check_device_extensions(state->physical_device, arena, device_extensions,
                            array_len(device_extensions), &device_extensions_found);

    bool validation_layer_found = false;
    check_validation_layers(arena, validation_layers, array_len(validation_layers),
                            &validation_layer_found);

    // Create Logical Device
    VkPhysicalDeviceFeatures device_feats   = { 0 };
    VkDeviceCreateInfo device_create_info   = { 0 };
    device_create_info.sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_create_info.pQueueCreateInfos    = queue_create_infos;
    device_create_info.queueCreateInfoCount = unique_families_count;
    device_create_info.pEnabledFeatures     = &device_feats;
    device_create_info.enabledLayerCount =
        validation_layer_found ? array_len(validation_layers) : 0;
    device_create_info.ppEnabledLayerNames     = validation_layers;
    device_create_info.ppEnabledExtensionNames = device_extensions;
    device_create_info.enabledExtensionCount =
        device_extensions_found ? array_len(device_extensions) : 0;

    if(vkCreateDevice(state->physical_device, &device_create_info, NULL, &state->device) !=
       VK_SUCCESS) {
        printf("Failed to create logical device!\n");
        exit(1);
    }
}

void vulkan_create_swapchain(VkState *state, Arena *arena, SDL_Window *window) {
    // Query Swapchain support
    VkSurfaceCapabilitiesKHR capabilities = { 0 };
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(state->physical_device, state->surface,
                                              &capabilities);

    unsigned int formats_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(state->physical_device, state->surface, &formats_count,
                                         NULL);
    VkSurfaceFormatKHR *formats =
        (VkSurfaceFormatKHR *)arena_push(arena, sizeof(VkSurfaceFormatKHR) * formats_count, 1);
    vkGetPhysicalDeviceSurfaceFormatsKHR(state->physical_device, state->surface, &formats_count,
                                         formats);

    unsigned int present_modes_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(state->physical_device, state->surface,
                                              &present_modes_count, NULL);
    VkPresentModeKHR *present_modes =
        (VkPresentModeKHR *)arena_push(arena, sizeof(VkPresentModeKHR) * present_modes_count, 1);
    vkGetPhysicalDeviceSurfacePresentModesKHR(state->physical_device, state->surface,
                                              &present_modes_count, present_modes);

    if(formats_count == 0 || present_modes_count == 0) {
        printf("Swap chain not supported\n");
        exit(1);
    }

    // NOTE: Choose swapchain surface format
    VkSurfaceFormatKHR format = formats[0];
    for(unsigned int format_index = 0; format_index < formats_count; ++format_index) {
        VkSurfaceFormatKHR available_format = formats[format_index];
        if(available_format.format == VK_FORMAT_B8G8R8A8_SRGB &&
           available_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            format = available_format;
            break;
        }
    }

    // NOTE: Choose swapchain presentation mode
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    for(unsigned int mode_index = 0; mode_index < present_modes_count; ++mode_index) {
        VkPresentModeKHR available_mode = present_modes[mode_index];
        if(available_mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            present_mode = available_mode;
            break;
        }
    }

    // NOTE: Choose Swapchain extends
    VkExtent2D extend = { 0 };
    if(capabilities.currentExtent.width != UINT32_MAX) {
        extend = capabilities.currentExtent;
    } else {
        int w, h;
        SDL_Vulkan_GetDrawableSize(window, &w, &h);
        extend.width  = clamp((unsigned int)w, capabilities.minImageExtent.width,
                              capabilities.maxImageExtent.width);
        extend.height = clamp((unsigned int)h, capabilities.minImageExtent.height,
                              capabilities.maxImageExtent.height);
    }

    // NOTE: Get image count
    unsigned int image_count = capabilities.minImageCount + 1;
    if(capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount) {
        image_count = capabilities.maxImageCount;
    }

    unsigned int queue_families[] = { state->present_queue_index, state->graphics_queue_index };

    // Create SwapChain
    VkSwapchainCreateInfoKHR swapchain_create_info = { 0 };
    swapchain_create_info.sType                    = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_create_info.surface                  = state->surface;
    swapchain_create_info.minImageCount            = image_count;
    swapchain_create_info.imageFormat              = format.format;
    swapchain_create_info.imageColorSpace          = format.colorSpace;
    swapchain_create_info.imageExtent              = extend;
    swapchain_create_info.imageArrayLayers         = 1;
    swapchain_create_info.imageUsage               = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    if(state->graphics_queue_index != state->present_queue_index) {
        swapchain_create_info.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        swapchain_create_info.queueFamilyIndexCount = 2;
        swapchain_create_info.pQueueFamilyIndices   = queue_families;
    } else {
        swapchain_create_info.imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE;
        swapchain_create_info.queueFamilyIndexCount = 0;
        swapchain_create_info.pQueueFamilyIndices   = NULL;
    }

    swapchain_create_info.preTransform   = capabilities.currentTransform;
    swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_create_info.presentMode    = present_mode;
    swapchain_create_info.clipped        = VK_TRUE;
    swapchain_create_info.oldSwapchain   = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain;
    if(vkCreateSwapchainKHR(state->device, &swapchain_create_info, NULL, &swapchain) !=
       VK_SUCCESS) {
        printf("Failed to create swap chain!\n");
        exit(1);
    }

    state->swapchain_image_format = format.format;
    state->swapchain_extent       = extend;
    state->swapchain              = swapchain;
}

void vulkan_create_images_views(VkState *state, Arena *arena) {
    // NOTE: Retrive swapchain images
    state->swapchain_images_count = 0;
    vkGetSwapchainImagesKHR(state->device, state->swapchain, &state->swapchain_images_count, NULL);
    VkImage *swapchain_images =
        (VkImage *)arena_push(arena, sizeof(VkImage) * state->swapchain_images_count, 1);
    vkGetSwapchainImagesKHR(state->device, state->swapchain, &state->swapchain_images_count,
                            swapchain_images);

    state->swapchain_images_views =
        (VkImageView *)arena_push(arena, sizeof(VkImageView) * state->swapchain_images_count, 1);

    // Create ImageView
    for(unsigned int image_index = 0; image_index < state->swapchain_images_count; ++image_index) {
        VkImageViewCreateInfo create_info           = { 0 };
        create_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        create_info.image                           = swapchain_images[image_index];
        create_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        create_info.format                          = state->swapchain_image_format;
        create_info.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        create_info.subresourceRange.baseMipLevel   = 0;
        create_info.subresourceRange.levelCount     = 1;
        create_info.subresourceRange.baseArrayLayer = 0;
        create_info.subresourceRange.layerCount     = 1;

        if(vkCreateImageView(state->device, &create_info, NULL,
                             &state->swapchain_images_views[image_index]) != VK_SUCCESS) {
            printf("Failed to create image views!\n");
            exit(1);
        }
    }
}

VkShaderModule vulkan_create_shader_module(VkDevice device, File *file) {
    // Create shader module
    VkShaderModuleCreateInfo create_info = { 0 };
    create_info.sType                    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize                 = file->size;
    create_info.pCode                    = (const unsigned int *)file->data;
    VkShaderModule shader_module;
    if(vkCreateShaderModule(device, &create_info, NULL, &shader_module) != VK_SUCCESS) {
        printf("Failed to create shader module!\n");
        exit(1);
    }
    return shader_module;
}

void vulkan_create_render_pass(VkState *state) {
    VkAttachmentDescription color_attachment = { 0 };
    color_attachment.format                  = state->swapchain_image_format;
    color_attachment.samples                 = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp                  = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp                 = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp           = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp          = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout             = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_attachment_ref = { 0 };
    color_attachment_ref.attachment            = 0;
    color_attachment_ref.layout                = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = { 0 };
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &color_attachment_ref;

    VkSubpassDependency dependency = { 0 };
    dependency.srcSubpass          = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass          = 0;
    dependency.srcStageMask        = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask       = 0;
    dependency.dstStageMask        = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo render_pass_info = { 0 };
    render_pass_info.sType                  = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount        = 1;
    render_pass_info.pAttachments           = &color_attachment;
    render_pass_info.subpassCount           = 1;
    render_pass_info.pSubpasses             = &subpass;
    render_pass_info.dependencyCount        = 1;
    render_pass_info.pDependencies          = &dependency;

    if(vkCreateRenderPass(state->device, &render_pass_info, NULL, &state->render_pass) !=
       VK_SUCCESS) {
        printf("Failed to create render pass!\n");
        exit(1);
    }
}

void vulkan_create_graphics_pipeline(VkState *state, Arena *arena) {

    // Create Graphics pipeline

    File vert_code = read_entire_file(arena, "./res/shaders/vert.spv");
    File frag_code = read_entire_file(arena, "./res/shaders/frag.spv");

    VkShaderModule vert_module = vulkan_create_shader_module(state->device, &vert_code);
    VkShaderModule frag_module = vulkan_create_shader_module(state->device, &frag_code);

    VkPipelineShaderStageCreateInfo vert_shader_stage_info = { 0 };
    vert_shader_stage_info.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_shader_stage_info.stage  = VK_SHADER_STAGE_VERTEX_BIT;
    vert_shader_stage_info.module = vert_module;
    vert_shader_stage_info.pName  = "main";

    VkPipelineShaderStageCreateInfo frag_shader_stage_info = { 0 };
    frag_shader_stage_info.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_shader_stage_info.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_shader_stage_info.module = frag_module;
    frag_shader_stage_info.pName  = "main";

    VkPipelineShaderStageCreateInfo shaders_stages_info[] = { vert_shader_stage_info,
                                                              frag_shader_stage_info };
    // Create Graphics pipeline

    VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic_state = { 0 };
    dynamic_state.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = array_len(dynamic_states);
    dynamic_state.pDynamicStates    = dynamic_states;

    VkVertexInputBindingDescription vertex_input_desc = vertex_get_binding_description();
    VkVertexInputAttributeDescription vertex_attr_desc[2];
    vertex_get_attribute_desc(vertex_attr_desc);

    VkPipelineVertexInputStateCreateInfo vertex_input_info = { 0 };
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_info.vertexBindingDescriptionCount   = 1;
    vertex_input_info.pVertexBindingDescriptions      = &vertex_input_desc;
    vertex_input_info.vertexAttributeDescriptionCount = array_len(vertex_attr_desc);
    vertex_input_info.pVertexAttributeDescriptions    = vertex_attr_desc;

    VkPipelineInputAssemblyStateCreateInfo input_assembly = { 0 };
    input_assembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport = { 0 };
    viewport.x          = 0.0f;
    viewport.y          = 0.0f;
    viewport.width      = (float)state->swapchain_extent.width;
    viewport.height     = (float)state->swapchain_extent.height;
    viewport.minDepth   = 0.0f;
    viewport.maxDepth   = 1.0f;

    VkRect2D scissor = { 0 };
    scissor.offset   = (VkOffset2D){ 0, 0 };
    scissor.extent   = state->swapchain_extent;

    VkPipelineViewportStateCreateInfo viewport_state = { 0 };
    viewport_state.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports    = &viewport;
    viewport_state.scissorCount  = 1;
    viewport_state.pScissors     = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer = { 0 };
    rasterizer.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable        = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode             = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth               = 1.0f;
    rasterizer.cullMode                = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace               = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable         = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0f;
    rasterizer.depthBiasClamp          = 0.0f;
    rasterizer.depthBiasSlopeFactor    = 0.0f;

    VkPipelineMultisampleStateCreateInfo multisampling = { 0 };
    multisampling.sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable   = VK_FALSE;
    multisampling.rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT;
    multisampling.minSampleShading      = 1.0f;
    multisampling.pSampleMask           = NULL;
    multisampling.alphaToCoverageEnable = VK_FALSE;
    multisampling.alphaToOneEnable      = VK_FALSE;

    VkPipelineColorBlendAttachmentState color_blend_attachment = { 0 };
    color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo color_blending = { 0 };
    color_blending.sType             = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.logicOpEnable     = VK_FALSE;
    color_blending.logicOp           = VK_LOGIC_OP_COPY;
    color_blending.attachmentCount   = 1;
    color_blending.pAttachments      = &color_blend_attachment;
    color_blending.blendConstants[0] = 0.0f;
    color_blending.blendConstants[1] = 0.0f;
    color_blending.blendConstants[2] = 0.0f;
    color_blending.blendConstants[3] = 0.0f;

    // Create Pipeline layout
    VkPipelineLayout pipeline_layout;
    VkPipelineLayoutCreateInfo pipeline_layout_info = { 0 };
    pipeline_layout_info.sType                      = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount             = 0;
    pipeline_layout_info.pSetLayouts                = NULL;
    pipeline_layout_info.pushConstantRangeCount     = 0;
    pipeline_layout_info.pPushConstantRanges        = NULL;

    if(vkCreatePipelineLayout(state->device, &pipeline_layout_info, NULL, &pipeline_layout) !=
       VK_SUCCESS) {
        printf("Failed to create pipeline layout!\n");
        exit(1);
    }

    VkGraphicsPipelineCreateInfo pipeline_info = { 0 };
    pipeline_info.sType                        = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount                   = 2;
    pipeline_info.pStages                      = shaders_stages_info;
    pipeline_info.pVertexInputState            = &vertex_input_info;
    pipeline_info.pInputAssemblyState          = &input_assembly;
    pipeline_info.pViewportState               = &viewport_state;
    pipeline_info.pRasterizationState          = &rasterizer;
    pipeline_info.pMultisampleState            = &multisampling;
    pipeline_info.pDepthStencilState           = NULL;
    pipeline_info.pColorBlendState             = &color_blending;
    pipeline_info.pDynamicState                = &dynamic_state;
    pipeline_info.layout                       = pipeline_layout;
    pipeline_info.renderPass                   = state->render_pass;
    pipeline_info.subpass                      = 0;
    pipeline_info.basePipelineHandle           = VK_NULL_HANDLE;
    pipeline_info.basePipelineIndex            = -1;

    if(vkCreateGraphicsPipelines(state->device, VK_NULL_HANDLE, 1, &pipeline_info, NULL,
                                 &state->pipeline) != VK_SUCCESS) {
        printf("Failed to create graphics pipeline!\n");
        exit(1);
    }
}

void vulkan_create_framebuffer(VkState *state, Arena *arena) {

    state->framebuffers_count = state->swapchain_images_count;
    state->framebuffers       = (VkFramebuffer *)arena_push(
        arena, sizeof(VkFramebuffer) * state->swapchain_images_count, 1);

    for(unsigned int image_index = 0; image_index < state->swapchain_images_count; ++image_index) {
        VkImageView image_view = state->swapchain_images_views[image_index];

        VkFramebufferCreateInfo framebuffer_info = { 0 };
        framebuffer_info.sType                   = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass              = state->render_pass;
        framebuffer_info.attachmentCount         = 1;
        framebuffer_info.pAttachments            = &image_view;
        framebuffer_info.width                   = state->swapchain_extent.width;
        framebuffer_info.height                  = state->swapchain_extent.height;
        framebuffer_info.layers                  = 1;

        if(vkCreateFramebuffer(state->device, &framebuffer_info, NULL,
                               &state->framebuffers[image_index]) != VK_SUCCESS) {
            printf("Failed to create framebuffer!\n");
            exit(1);
        }
    }
}

void vulkan_cleanup_swapchain(VkState *state) {
    for(unsigned int buffer_index = 0; buffer_index < state->framebuffers_count; ++buffer_index) {
        vkDestroyFramebuffer(state->device, state->framebuffers[buffer_index], NULL);
    }

    for(unsigned int image_index = 0; image_index < state->swapchain_images_count; ++image_index) {
        vkDestroyImageView(state->device, state->swapchain_images_views[image_index], NULL);
    }

    vkDestroySwapchainKHR(state->device, state->swapchain, NULL);
}

void vulkan_recreate_swapchain(VkState *state, Arena *arena, SDL_Window *window) {

    while(SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
        SDL_Event e;
        SDL_WaitEvent(&e);
    }

    vkDeviceWaitIdle(state->device);

    vulkan_cleanup_swapchain(state);

    vulkan_create_swapchain(state, arena, window);
    vulkan_create_images_views(state, arena);
    vulkan_create_framebuffer(state, arena);
}

void vulkan_create_command_pool(VkState *state) {

    VkCommandPoolCreateInfo pool_info = { 0 };
    pool_info.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags                   = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex        = state->graphics_queue_index;

    if(vkCreateCommandPool(state->device, &pool_info, NULL, &state->command_pool) != VK_SUCCESS) {
        printf("Failed to create command pool!\n");
        exit(1);
    }
}

void vulkan_create_command_buffer(VkState *state) {
    VkCommandBufferAllocateInfo alloc_info = { 0 };
    alloc_info.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool                 = state->command_pool;
    alloc_info.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount          = array_len(state->command_buffers);

    if(vkAllocateCommandBuffers(state->device, &alloc_info, state->command_buffers) != VK_SUCCESS) {
        printf("Failed to allocate command buffers!\n");
        exit(1);
    }
}

void recordCommandBuffer(VkState *state, VkCommandBuffer command_buffer, uint32_t image_index) {
    VkCommandBufferBeginInfo begin_info = { 0 };
    begin_info.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags                    = 0;
    begin_info.pInheritanceInfo         = NULL;

    if(vkBeginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS) {
        printf("Failed to begin recording command buffer!\n");
        exit(1);
    }

    VkRenderPassBeginInfo render_pass_info = { 0 };
    render_pass_info.sType                 = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass            = state->render_pass;
    render_pass_info.framebuffer           = state->framebuffers[image_index];
    render_pass_info.renderArea.offset     = (VkOffset2D){ 0, 0 };
    render_pass_info.renderArea.extent     = state->swapchain_extent;

    VkClearValue clear_color         = { { { 0.0f, 0.0f, 0.0f, 1.0f } } };
    render_pass_info.clearValueCount = 1;
    render_pass_info.pClearValues    = &clear_color;

    vkCmdBeginRenderPass(command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, state->pipeline);

    VkViewport viewport = { 0 };
    viewport.x          = 0.0f;
    viewport.y          = 0.0f;
    viewport.width      = (float)state->swapchain_extent.width;
    viewport.height     = (float)state->swapchain_extent.height;
    viewport.minDepth   = 0.0f;
    viewport.maxDepth   = 1.0f;
    vkCmdSetViewport(command_buffer, 0, 1, &viewport);

    VkRect2D scissor = { 0 };
    scissor.offset   = (VkOffset2D){ 0, 0 };
    scissor.extent   = state->swapchain_extent;
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);

    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(command_buffer, 0, 1, &state->vertex_buffer, offsets);

    vkCmdDraw(command_buffer, array_len(vertices), 1, 0, 0);

    vkCmdEndRenderPass(command_buffer);

    if(vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
        printf("Failed to record command buffer!\n");
        exit(1);
    }
}

void vulkan_create_sync_objs(VkState *state) {
    VkSemaphoreCreateInfo semaphore_info = { 0 };
    semaphore_info.sType                 = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info = { 0 };
    fence_info.sType             = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags             = VK_FENCE_CREATE_SIGNALED_BIT;

    for(unsigned int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if(vkCreateSemaphore(state->device, &semaphore_info, NULL,
                             &state->image_available_semaphores[i]) != VK_SUCCESS ||
           vkCreateSemaphore(state->device, &semaphore_info, NULL,
                             &state->render_finished_semaphores[i]) != VK_SUCCESS ||
           vkCreateFence(state->device, &fence_info, NULL, &state->in_flight_fences[i]) !=
               VK_SUCCESS) {
            printf("Failed to create semaphores!\n");
            exit(1);
        }
    }
}

void vulkan_draw_frame(VkState *state, Arena *arena, SDL_Window *window, VkQueue present_queue,
                       VkQueue graphics_queue) {

    VkCommandBuffer command_buffer        = state->command_buffers[state->current_frame];
    VkFence in_flight_fence               = state->in_flight_fences[state->current_frame];
    VkSemaphore image_available_semaphore = state->image_available_semaphores[state->current_frame];
    VkSemaphore render_finished_semaphore = state->render_finished_semaphores[state->current_frame];

    state->current_frame = (state->current_frame + 1) % MAX_FRAMES_IN_FLIGHT;

    // Draw Frame
    vkWaitForFences(state->device, 1, &in_flight_fence, VK_TRUE, UINT64_MAX);

    unsigned int image_index = 0;
    VkResult result =
        vkAcquireNextImageKHR(state->device, state->swapchain, UINT64_MAX,
                              image_available_semaphore, VK_NULL_HANDLE, &image_index);
    if(result == VK_ERROR_OUT_OF_DATE_KHR) {
        vulkan_recreate_swapchain(state, arena, window);
        return;
    } else if((result != VK_SUCCESS) && (result != VK_SUBOPTIMAL_KHR)) {
        printf("Failed to acquire swap chain image!\n");
        exit(1);
    }
    vkResetFences(state->device, 1, &in_flight_fence);

    vkResetCommandBuffer(command_buffer, 0);
    recordCommandBuffer(state, command_buffer, image_index);

    VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSubmitInfo submit_info           = { 0 };
    submit_info.sType                  = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount     = 1;
    submit_info.pWaitSemaphores        = &image_available_semaphore;
    submit_info.pWaitDstStageMask      = wait_stages;
    submit_info.commandBufferCount     = 1;
    submit_info.pCommandBuffers        = &command_buffer;
    submit_info.signalSemaphoreCount   = 1;
    submit_info.pSignalSemaphores      = &render_finished_semaphore;

    if(vkQueueSubmit(graphics_queue, 1, &submit_info, in_flight_fence) != VK_SUCCESS) {
        printf("Failed to submit draw command buffer!\n");
        exit(1);
    }

    VkPresentInfoKHR present_info = { 0 };
    present_info.sType            = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores    = &render_finished_semaphore;
    present_info.swapchainCount     = 1;
    present_info.pSwapchains        = &state->swapchain;
    present_info.pImageIndices      = &image_index;
    present_info.pResults           = NULL;

    result = vkQueuePresentKHR(present_queue, &present_info);

    if(result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
       state->framebuffer_resized) {
        state->framebuffer_resized = false;
        vulkan_recreate_swapchain(state, arena, window);
        return;
    } else if(result != VK_SUCCESS) {
        printf("Failed to present swap chain image!\n");
        exit(1);
    }
}

unsigned int find_memory_type(VkPhysicalDevice physical_device, uint32_t filter,
                              VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_properties);
    for(uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
        if((filter & (1 << i)) &&
           (mem_properties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    printf("Failed to find suitable memory type!\n");
    exit(1);
}

void vulkan_create_vertex_buffer(VkState *state) {
    VkBufferCreateInfo buffer_info = { 0 };
    buffer_info.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size               = sizeof(vertices);
    buffer_info.usage              = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    buffer_info.sharingMode        = VK_SHARING_MODE_EXCLUSIVE;

    if(vkCreateBuffer(state->device, &buffer_info, NULL, &state->vertex_buffer) != VK_SUCCESS) {
        printf("Failed to create vertex buffer!\n");
        exit(1);
    }

    VkMemoryRequirements mem_req;
    vkGetBufferMemoryRequirements(state->device, state->vertex_buffer, &mem_req);

    VkMemoryAllocateInfo alloc_info = { 0 };
    alloc_info.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize       = mem_req.size;
    alloc_info.memoryTypeIndex = find_memory_type(state->physical_device, mem_req.memoryTypeBits,
                                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if(vkAllocateMemory(state->device, &alloc_info, NULL, &state->vertex_buffer_memory) !=
       VK_SUCCESS) {
        printf("Failed to allocate vertex buffer memory!\n");
        exit(1);
    }

    vkBindBufferMemory(state->device, state->vertex_buffer, state->vertex_buffer_memory, 0);

    void *data;
    vkMapMemory(state->device, state->vertex_buffer_memory, 0, buffer_info.size, 0, &data);
    memcpy(data, vertices, buffer_info.size);
    vkUnmapMemory(state->device, state->vertex_buffer_memory);
}

int main(void) {

    // Application Setup
    Arena arena = arena_create(mb(100));
    SDL_Init(SDL_INIT_VIDEO);

    // Create SDL2 Window
    int w        = 1920 / 2;
    int h        = 1080 / 2;
    bool running = true;

    SDL_Window *window = SDL_CreateWindow(
        "vulkan (hello, triangle!)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h,
        SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    VkState state = { 0 };

    vulkan_create_instance(&state, &arena, window);
    vulkan_create_surface(&state, window);
    vulkan_select_physical_device(&state, &arena);
    vulkan_find_family_queues(&state, &arena);
    vulkan_create_logical_device(&state, &arena);
    vulkan_create_swapchain(&state, &arena, window);
    vulkan_create_images_views(&state, &arena);
    vulkan_create_render_pass(&state);
    vulkan_create_graphics_pipeline(&state, &arena);
    vulkan_create_framebuffer(&state, &arena);

    vulkan_create_command_pool(&state);
    vulkan_create_command_buffer(&state);
    vulkan_create_sync_objs(&state);

    vulkan_create_vertex_buffer(&state);

    printf("frambuffer count: %d\n", state.framebuffers_count);
    printf("Total allocated size: %zu\n", arena.used);

    // Retrive Graphics queue
    VkQueue present_queue, graphics_queue;
    vkGetDeviceQueue(state.device, state.present_queue_index, 0, &present_queue);
    vkGetDeviceQueue(state.device, state.graphics_queue_index, 0, &graphics_queue);

    while(running) {

        arena_clear(&arena);

        SDL_Event e;
        while(SDL_PollEvent(&e)) {
            switch(e.type) {
            case SDL_QUIT: {
                running = false;
            } break;
            case SDL_WINDOWEVENT: {
                if(e.window.event == SDL_WINDOWEVENT_RESIZED) {
                    state.framebuffer_resized = true;
                }
                if(e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    state.framebuffer_resized = true;
                }
            }
            }
        }

        vulkan_draw_frame(&state, &arena, window, present_queue, graphics_queue);
    }

    vkDeviceWaitIdle(state.device);

    return 0;
}
