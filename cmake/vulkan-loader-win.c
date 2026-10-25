// Resolve the driver-installed Vulkan loader without requiring a Vulkan SDK
// import library or shipping a second copy of vulkan-1.dll.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

static INIT_ONCE loader_once = INIT_ONCE_STATIC_INIT;
static PFN_vkGetInstanceProcAddr get_instance_proc_addr;
static HMODULE vulkan_module;

static BOOL CALLBACK load_vulkan(PINIT_ONCE once, PVOID param, PVOID *context)
{
    vulkan_module = LoadLibraryExW(L"vulkan-1.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (vulkan_module)
        get_instance_proc_addr = (PFN_vkGetInstanceProcAddr)
            GetProcAddress(vulkan_module, "vkGetInstanceProcAddr");
    // The module stays loaded for the lifetime of all Vulkan objects.
    return TRUE;
}

// mpv also calls a small set of loader-exported instance functions directly.
// Resolve these from the system loader; no Vulkan SDK or import library is needed.
#define LOAD_FUNCTION(name) \
    InitOnceExecuteOnce(&loader_once, load_vulkan, NULL, NULL); \
    PFN_##name function = vulkan_module ? \
        (PFN_##name)GetProcAddress(vulkan_module, #name) : NULL

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(
    VkInstance instance, uint32_t *count, VkPhysicalDevice *devices)
{
    LOAD_FUNCTION(vkEnumeratePhysicalDevices);
    return function ? function(instance, count, devices) : VK_ERROR_INITIALIZATION_FAILED;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties2(
    VkPhysicalDevice device, VkPhysicalDeviceProperties2 *properties)
{
    LOAD_FUNCTION(vkGetPhysicalDeviceProperties2);
    if (function)
        function(device, properties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties2(
    VkPhysicalDevice device, uint32_t *count, VkQueueFamilyProperties2 *properties)
{
    LOAD_FUNCTION(vkGetPhysicalDeviceQueueFamilyProperties2);
    if (function)
        function(device, count, properties);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateWin32SurfaceKHR(
    VkInstance instance, const VkWin32SurfaceCreateInfoKHR *info,
    const VkAllocationCallbacks *allocator, VkSurfaceKHR *surface)
{
    LOAD_FUNCTION(vkCreateWin32SurfaceKHR);
    return function ? function(instance, info, allocator, surface) : VK_ERROR_INITIALIZATION_FAILED;
}

VKAPI_ATTR void VKAPI_CALL vkDestroySurfaceKHR(
    VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks *allocator)
{
    LOAD_FUNCTION(vkDestroySurfaceKHR);
    if (function)
        function(instance, surface, allocator);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(
    VkInstance instance, const char *name)
{
    InitOnceExecuteOnce(&loader_once, load_vulkan, NULL, NULL);
    return get_instance_proc_addr ? get_instance_proc_addr(instance, name) : NULL;
}
