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
#define unused(v) ((void)(v))
#define kb(x) ((x) * 1024ll)
#define mb(x) (kb(x) * 1024ll)
#define gb(x) (mb(x) * 1024ll)

#define clamp(a, b, c) max(min(a, c), b)

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

// TODO: Push align memory to the arena
void *arena_push(Arena *arena, size_t size) {
    assert(arena->used + size <= arena->size);
    unsigned char *result = (unsigned char *)arena->data + arena->used;
    arena->used += size;
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
    result.data = arena_push(arena, size + 1);
    result.size = size;
    fread(result.data, result.size, 1, file);
    ((unsigned char *)result.data)[result.size] = '\0';
    fclose(file);
    return result;
}

const char *validation_layers[] = { "VK_LAYER_KHRONOS_validation" };
const char *device_extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

void check_device_extensions(VkPhysicalDevice device, Arena *arena, const char **extensions,
                             unsigned extensions_count, bool *extensions_found) {
    *extensions_found = true;

    unsigned int device_extension_count = 0;
    vkEnumerateDeviceExtensionProperties(device, NULL, &device_extension_count, NULL);
    VkExtensionProperties *device_extension_props = (VkExtensionProperties *)arena_push(
        arena, sizeof(VkExtensionProperties) * device_extension_count);
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
        (VkLayerProperties *)arena_push(arena, sizeof(VkLayerProperties) * layers_count);
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

VkInstance vulkan_create_instance(Arena *arena, SDL_Window *window) {
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
        (const char **)arena_push(arena, instance_extensions_count * sizeof(const char **));
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

    VkInstance instace = { 0 };
    if(vkCreateInstance(&create_info, NULL, &instace) != VK_SUCCESS) {
        printf("Fail to create VkInstace\n");
        exit(1);
    }

    return instace;
}

VkSurfaceKHR vulkan_create_surface(SDL_Window *window, VkInstance instance) {
    // Create Window Surface
    VkSurfaceKHR surface;
    if(SDL_Vulkan_CreateSurface(window, instance, &surface) == SDL_FALSE) {
        printf("Fail to create Vulkan surface\n");
        exit(1);
    }
    return surface;
}

VkPhysicalDevice vulkan_select_physical_device(Arena *arena, VkInstance instance) {
    // Selecting a physical device
    unsigned int device_count = 0;
    vkEnumeratePhysicalDevices(instance, &device_count, NULL);
    if(device_count == 0) {
        printf("Fail to find GPU with vulkan support\n");
        exit(1);
    }
    VkPhysicalDevice *physical_devices =
        (VkPhysicalDevice *)arena_push(arena, sizeof(VkPhysicalDevice) * device_count);
    vkEnumeratePhysicalDevices(instance, &device_count, physical_devices);

    // NOTE: Find suitable device
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;

    for(unsigned int device_index = 0; device_index < device_count; ++device_index) {

        VkPhysicalDevice device = physical_devices[device_index];
        VkPhysicalDeviceProperties device_props;
        VkPhysicalDeviceFeatures device_feats;
        vkGetPhysicalDeviceProperties(device, &device_props);
        vkGetPhysicalDeviceFeatures(device, &device_feats);

        if(device_props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
           device_feats.geometryShader) {
            physical_device = device;
            break;
        }
    }

    assert(physical_device != VK_NULL_HANDLE);
    return physical_device;
}

void vulkan_find_family_queues(Arena *arena, VkPhysicalDevice physical_device, VkSurfaceKHR surface,
                               unsigned int *graphics_queue_index,
                               unsigned int *present_queue_index,
                               unsigned int *queue_family_count) {
    // NOTE: Find queue family queues
    *queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, queue_family_count, NULL);
    VkQueueFamilyProperties *queue_family_props =
        arena_push(arena, sizeof(VkQueueFamilyProperties) * *queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, queue_family_count,
                                             queue_family_props);
    *graphics_queue_index = (unsigned int)-1;
    *present_queue_index  = (unsigned int)-1;

    for(unsigned int i = 0; i < *queue_family_count; ++i) {

        VkBool32 present_support = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, i, surface, &present_support);
        if(present_support) {
            *present_queue_index = i;
        }

        if(queue_family_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            *graphics_queue_index = i;
        }

        if(*present_queue_index != (unsigned int)-1 && *graphics_queue_index != (unsigned int)-1) {
            break;
        }
    }

    if(*graphics_queue_index == (unsigned int)-1 || *present_queue_index == (unsigned int)-1) {
        printf("Present Queue not supported!\n");
        exit(1);
    }
}

VkDevice vulkan_create_logical_device(Arena *arena, VkPhysicalDevice physical_device,
                                      unsigned int present_queue_index,
                                      unsigned int graphics_queue_index,
                                      unsigned int queue_family_count) {
    float queue_priority = 1.0f;

    unsigned int queue_families[]               = { present_queue_index, graphics_queue_index };
    VkDeviceQueueCreateInfo *queue_create_infos = (VkDeviceQueueCreateInfo *)arena_push(
        arena, sizeof(VkDeviceQueueCreateInfo) * array_len(queue_families));
    unsigned int unique_families_count = 0;
    bool *unique_families_state        = arena_push(arena, sizeof(bool) * queue_family_count);
    memset(unique_families_state, 0, sizeof(bool) * queue_family_count);

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
    check_device_extensions(physical_device, arena, device_extensions, array_len(device_extensions),
                            &device_extensions_found);

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

    VkDevice device;
    if(vkCreateDevice(physical_device, &device_create_info, NULL, &device) != VK_SUCCESS) {
        printf("Failed to create logical device!\n");
        exit(1);
    }

    return device;
}

VkSwapchainKHR vulkan_create_swapchain(Arena *arena, VkPhysicalDevice physical_device,
                                       VkDevice device, VkSurfaceKHR surface, SDL_Window *window,
                                       unsigned int present_queue_index,
                                       unsigned int graphics_queue_index,
                                       VkFormat *swapchain_image_format,
                                       VkExtent2D *swapchain_extent) {
    // Query Swapchain support
    VkSurfaceCapabilitiesKHR capabilities = { 0 };
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &capabilities);

    unsigned int formats_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &formats_count, NULL);
    VkSurfaceFormatKHR *formats =
        (VkSurfaceFormatKHR *)arena_push(arena, sizeof(VkSurfaceFormatKHR) * formats_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &formats_count, formats);

    unsigned int present_modes_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &present_modes_count, NULL);
    VkPresentModeKHR *present_modes =
        (VkPresentModeKHR *)arena_push(arena, sizeof(VkPresentModeKHR) * present_modes_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &present_modes_count,
                                              present_modes);

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

    unsigned int queue_families[] = { present_queue_index, graphics_queue_index };

    // Create SwapChain
    VkSwapchainCreateInfoKHR swapchain_create_info = { 0 };
    swapchain_create_info.sType                    = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_create_info.surface                  = surface;
    swapchain_create_info.minImageCount            = image_count;
    swapchain_create_info.imageFormat              = format.format;
    swapchain_create_info.imageColorSpace          = format.colorSpace;
    swapchain_create_info.imageExtent              = extend;
    swapchain_create_info.imageArrayLayers         = 1;
    swapchain_create_info.imageUsage               = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    if(graphics_queue_index != present_queue_index) {
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
    if(vkCreateSwapchainKHR(device, &swapchain_create_info, NULL, &swapchain) != VK_SUCCESS) {
        printf("Failed to create swap chain!\n");
        exit(1);
    }

    *swapchain_image_format = format.format;
    *swapchain_extent       = extend;

    return swapchain;
}

void vulkan_create_images_views(Arena *arena, VkDevice device, VkSwapchainKHR swapchain,
                                VkFormat swapchain_image_format,
                                VkImageView **swapchain_images_views,
                                unsigned int *swapchain_images_count) {
    // NOTE: Retrive swapchain images
    *swapchain_images_count = 0;
    vkGetSwapchainImagesKHR(device, swapchain, swapchain_images_count, NULL);
    VkImage *swapchain_images =
        (VkImage *)arena_push(arena, sizeof(VkImage) * *swapchain_images_count);
    vkGetSwapchainImagesKHR(device, swapchain, swapchain_images_count, swapchain_images);

    *swapchain_images_views =
        (VkImageView *)arena_push(arena, sizeof(VkImageView) * *swapchain_images_count);

    // Create ImageView
    for(unsigned int image_index = 0; image_index < *swapchain_images_count; ++image_index) {
        VkImageViewCreateInfo create_info           = { 0 };
        create_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        create_info.image                           = swapchain_images[image_index];
        create_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        create_info.format                          = swapchain_image_format;
        create_info.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        create_info.subresourceRange.baseMipLevel   = 0;
        create_info.subresourceRange.levelCount     = 1;
        create_info.subresourceRange.baseArrayLayer = 0;
        create_info.subresourceRange.layerCount     = 1;

        if(vkCreateImageView(device, &create_info, NULL,
                             &((*swapchain_images_views)[image_index])) != VK_SUCCESS) {
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

VkRenderPass vulkan_create_render_pass(VkDevice device, VkFormat swapchain_image_format) {
    VkAttachmentDescription color_attachment = { 0 };
    color_attachment.format                  = swapchain_image_format;
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

    VkRenderPass render_pass = { 0 };

    VkRenderPassCreateInfo render_pass_info = { 0 };
    render_pass_info.sType                  = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount        = 1;
    render_pass_info.pAttachments           = &color_attachment;
    render_pass_info.subpassCount           = 1;
    render_pass_info.pSubpasses             = &subpass;
    render_pass_info.dependencyCount        = 1;
    render_pass_info.pDependencies          = &dependency;

    if(vkCreateRenderPass(device, &render_pass_info, NULL, &render_pass) != VK_SUCCESS) {
        printf("Failed to create render pass!\n");
        exit(1);
    }

    return render_pass;
}

VkPipeline vulkan_create_graphics_pipeline(Arena *arena, VkDevice device,
                                           VkExtent2D swapchain_extet, VkRenderPass render_pass) {

    // Create Graphics pipeline

    File vert_code = read_entire_file(arena, "./res/shaders/vert.spv");
    File frag_code = read_entire_file(arena, "./res/shaders/frag.spv");

    VkShaderModule vert_module = vulkan_create_shader_module(device, &vert_code);
    VkShaderModule frag_module = vulkan_create_shader_module(device, &frag_code);

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

    VkPipelineVertexInputStateCreateInfo vertex_input_info = { 0 };
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_info.vertexBindingDescriptionCount   = 0;
    vertex_input_info.pVertexBindingDescriptions      = NULL;
    vertex_input_info.vertexAttributeDescriptionCount = 0;
    vertex_input_info.pVertexAttributeDescriptions    = NULL;

    VkPipelineInputAssemblyStateCreateInfo input_assembly = { 0 };
    input_assembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport = { 0 };
    viewport.x          = 0.0f;
    viewport.y          = 0.0f;
    viewport.width      = (float)swapchain_extet.width;
    viewport.height     = (float)swapchain_extet.height;
    viewport.minDepth   = 0.0f;
    viewport.maxDepth   = 1.0f;

    VkRect2D scissor = { 0 };
    scissor.offset   = (VkOffset2D){ 0, 0 };
    scissor.extent   = swapchain_extet;

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

    if(vkCreatePipelineLayout(device, &pipeline_layout_info, NULL, &pipeline_layout) !=
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
    pipeline_info.renderPass                   = render_pass;
    pipeline_info.subpass                      = 0;
    pipeline_info.basePipelineHandle           = VK_NULL_HANDLE;
    pipeline_info.basePipelineIndex            = -1;

    VkPipeline pipeline;
    if(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &pipeline) !=
       VK_SUCCESS) {
        printf("Failed to create graphics pipeline!\n");
        exit(1);
    }

    return pipeline;
}

void vulkan_create_framebuffer(Arena *arena, VkDevice device, VkImageView *swapchain_images_views,
                               unsigned int swapchain_images_count, VkRenderPass render_pass,
                               VkExtent2D swapchain_extent, VkFramebuffer **framebuffers,
                               unsigned int *framebuffers_count) {

    *framebuffers_count = swapchain_images_count;
    *framebuffers =
        (VkFramebuffer *)arena_push(arena, sizeof(VkFramebuffer) * swapchain_images_count);

    for(unsigned int image_index = 0; image_index < swapchain_images_count; ++image_index) {
        VkImageView image_view = swapchain_images_views[image_index];

        VkFramebufferCreateInfo framebuffer_info = { 0 };
        framebuffer_info.sType                   = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass              = render_pass;
        framebuffer_info.attachmentCount         = 1;
        framebuffer_info.pAttachments            = &image_view;
        framebuffer_info.width                   = swapchain_extent.width;
        framebuffer_info.height                  = swapchain_extent.height;
        framebuffer_info.layers                  = 1;

        if(vkCreateFramebuffer(device, &framebuffer_info, NULL, &((*framebuffers)[image_index])) !=
           VK_SUCCESS) {
            printf("Failed to create framebuffer!\n");
            exit(1);
        }
    }
}

VkCommandPool vulkan_create_command_pool(VkDevice device, unsigned int graphics_queue_index) {

    VkCommandPoolCreateInfo pool_info = { 0 };
    pool_info.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags                   = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex        = graphics_queue_index;

    VkCommandPool command_pool = { 0 };
    if(vkCreateCommandPool(device, &pool_info, NULL, &command_pool) != VK_SUCCESS) {
        printf("Failed to create command pool!\n");
        exit(1);
    }
    return command_pool;
}

VkCommandBuffer vulkan_create_command_buffer(VkDevice device, VkCommandPool command_pool) {
    VkCommandBufferAllocateInfo alloc_info = { 0 };
    alloc_info.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool                 = command_pool;
    alloc_info.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount          = 1;

    VkCommandBuffer command_buffer = { 0 };
    if(vkAllocateCommandBuffers(device, &alloc_info, &command_buffer) != VK_SUCCESS) {
        printf("Failed to allocate command buffers!\n");
        exit(1);
    }
    return command_buffer;
}

void recordCommandBuffer(VkCommandBuffer command_buffer, VkPipeline pipeline,
                         VkFramebuffer *framebuffers, uint32_t image_index,
                         VkRenderPass render_pass, VkExtent2D swapchain_extent) {
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
    render_pass_info.renderPass            = render_pass;
    render_pass_info.framebuffer           = framebuffers[image_index];
    render_pass_info.renderArea.offset     = (VkOffset2D){ 0, 0 };
    render_pass_info.renderArea.extent     = swapchain_extent;

    VkClearValue clear_color         = { { { 0.0f, 0.0f, 0.0f, 1.0f } } };
    render_pass_info.clearValueCount = 1;
    render_pass_info.pClearValues    = &clear_color;

    vkCmdBeginRenderPass(command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    VkViewport viewport = { 0 };
    viewport.x          = 0.0f;
    viewport.y          = 0.0f;
    viewport.width      = (float)swapchain_extent.width;
    viewport.height     = (float)swapchain_extent.height;
    viewport.minDepth   = 0.0f;
    viewport.maxDepth   = 1.0f;
    vkCmdSetViewport(command_buffer, 0, 1, &viewport);

    VkRect2D scissor = { 0 };
    scissor.offset   = (VkOffset2D){ 0, 0 };
    scissor.extent   = swapchain_extent;
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);

    vkCmdDraw(command_buffer, 3, 1, 0, 0);

    vkCmdEndRenderPass(command_buffer);

    if(vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
        printf("Failed to record command buffer!\n");
        exit(1);
    }
}

void vulkan_create_sync_objs(VkDevice device, VkSemaphore *image_available_semaphore,
                             VkSemaphore *render_finished_semaphore, VkFence *in_flight_fence) {
    VkSemaphoreCreateInfo semaphore_info = { 0 };
    semaphore_info.sType                 = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info = { 0 };
    fence_info.sType             = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags             = VK_FENCE_CREATE_SIGNALED_BIT;

    if(vkCreateSemaphore(device, &semaphore_info, NULL, image_available_semaphore) != VK_SUCCESS ||
       vkCreateSemaphore(device, &semaphore_info, NULL, render_finished_semaphore) != VK_SUCCESS ||
       vkCreateFence(device, &fence_info, NULL, in_flight_fence) != VK_SUCCESS) {
        printf("Failed to create semaphores!\n");
        exit(1);
    }
}

int main(void) {

    // Application Setup
    Arena arena = arena_create(mb(100));
    SDL_Init(SDL_INIT_VIDEO);

    // Create SDL2 Window
    int w        = 1920 / 2;
    int h        = 1080 / 2;
    bool running = true;

    SDL_Window *window = SDL_CreateWindow("vulkan", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          w, h, SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN);

    VkInstance instance              = vulkan_create_instance(&arena, window);
    VkSurfaceKHR surface             = vulkan_create_surface(window, instance);
    VkPhysicalDevice physical_device = vulkan_select_physical_device(&arena, instance);
    unsigned int present_queue_index, graphics_queue_index, queue_family_count;
    vulkan_find_family_queues(&arena, physical_device, surface, &graphics_queue_index,
                              &present_queue_index, &queue_family_count);
    VkDevice device = vulkan_create_logical_device(&arena, physical_device, present_queue_index,
                                                   graphics_queue_index, queue_family_count);

    VkFormat swapchain_image_format = { 0 };
    VkExtent2D swapchain_extet      = { 0 };
    VkSwapchainKHR swapchain        = vulkan_create_swapchain(
        &arena, physical_device, device, surface, window, present_queue_index, graphics_queue_index,
        &swapchain_image_format, &swapchain_extet);

    unsigned int swapchain_images_count = 0;
    VkImageView *swapchain_images_views = NULL;
    vulkan_create_images_views(&arena, device, swapchain, swapchain_image_format,
                               &swapchain_images_views, &swapchain_images_count);
    VkRenderPass render_pass = vulkan_create_render_pass(device, swapchain_image_format);
    VkPipeline pipeline =
        vulkan_create_graphics_pipeline(&arena, device, swapchain_extet, render_pass);

    VkFramebuffer *framebuffers     = NULL;
    unsigned int framebuffers_count = 0;
    vulkan_create_framebuffer(&arena, device, swapchain_images_views, swapchain_images_count,
                              render_pass, swapchain_extet, &framebuffers, &framebuffers_count);

    VkCommandPool command_pool     = vulkan_create_command_pool(device, graphics_queue_index);
    VkCommandBuffer command_buffer = vulkan_create_command_buffer(device, command_pool);

    VkSemaphore image_available_semaphore;
    VkSemaphore render_finished_semaphore;
    VkFence in_flight_fence;
    vulkan_create_sync_objs(device, &image_available_semaphore, &render_finished_semaphore,
                            &in_flight_fence);

    printf("frambuffer count: %d\n", framebuffers_count);
    printf("Total allocated size: %zu\n", arena.used);

    // Retrive Graphics queue
    VkQueue present_queue, graphics_queue;
    vkGetDeviceQueue(device, present_queue_index, 0, &present_queue);
    vkGetDeviceQueue(device, graphics_queue_index, 0, &graphics_queue);

    while(running) {
        SDL_Event e;
        while(SDL_PollEvent(&e)) {
            switch(e.type) {
            case SDL_QUIT: {
                running = false;
            } break;
            }
        }

        // Draw Frame
        vkWaitForFences(device, 1, &in_flight_fence, VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1, &in_flight_fence);

        unsigned int image_index = 0;
        vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, image_available_semaphore,
                              VK_NULL_HANDLE, &image_index);

        vkResetCommandBuffer(command_buffer, 0);
        recordCommandBuffer(command_buffer, pipeline, framebuffers, image_index, render_pass,
                            swapchain_extet);

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
        present_info.pSwapchains        = &swapchain;
        present_info.pImageIndices      = &image_index;
        present_info.pResults           = NULL;

        vkQueuePresentKHR(present_queue, &present_info);
    }

    return 0;
}
