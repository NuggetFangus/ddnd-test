/* Sonda de Vulkan para o R36S: carrega a libvulkan por dlopen e grava o que o driver oferece
 * em vkprobe.txt (na mesma pasta do executável). Não precisa de tela nem de SDL. */
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libgen.h>
#include <unistd.h>

static FILE *out;
#define P(...) do { printf(__VA_ARGS__); if (out) { fprintf(out, __VA_ARGS__); fflush(out); } } while (0)

static void dump_file(const char *path, const char *tag, int maxlines) {
    FILE *f = fopen(path, "r"); char l[256]; int n = 0;
    if (!f) { P("%s: (indisponível)\n", tag); return; }
    P("%s:\n", tag);
    while (fgets(l, sizeof l, f) && n++ < maxlines) P("  %s", l);
    fclose(f);
}

int main(int argc, char **argv) {
    char path[1024], self[1024];
    ssize_t k = readlink("/proc/self/exe", self, sizeof self - 1);
    if (k > 0) { self[k] = 0; snprintf(path, sizeof path, "%s/vkprobe.txt", dirname(self)); }
    else snprintf(path, sizeof path, "vkprobe.txt");
    out = fopen(path, "w");
    P("== vkprobe ==\n");
    dump_file("/proc/version", "kernel", 2);
    dump_file("/etc/os-release", "sistema", 3);
    dump_file("/proc/meminfo", "memória", 3);
    P("libs de GPU visíveis:\n");
    const char *libs[] = {"libvulkan.so.1", "libvulkan.so", "libmali.so.1", "libmali.so", "libEGL.so.1", "libGLESv2.so.2", "libgbm.so.1", "libdrm.so.2", 0};
    void *vk = 0;
    for (int i = 0; libs[i]; i++) {
        void *h = dlopen(libs[i], RTLD_NOW);
        P("  %-16s %s\n", libs[i], h ? "ok" : dlerror());
        if (h && !vk && strstr(libs[i], "vulkan")) vk = h;
    }
    if (!vk) { P("RESULTADO: sem libvulkan - o Vulkan não está instalado/visível neste sistema.\n"); return 1; }

    PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)dlsym(vk, "vkGetInstanceProcAddr");
    if (!gipa) { P("sem vkGetInstanceProcAddr\n"); return 1; }
    PFN_vkEnumerateInstanceVersion eiv = (PFN_vkEnumerateInstanceVersion)gipa(0, "vkEnumerateInstanceVersion");
    uint32_t iv = VK_API_VERSION_1_0; if (eiv) eiv(&iv);
    P("versão do loader/instância: %u.%u.%u\n", VK_VERSION_MAJOR(iv), VK_VERSION_MINOR(iv), VK_VERSION_PATCH(iv));

    PFN_vkEnumerateInstanceExtensionProperties eie = (PFN_vkEnumerateInstanceExtensionProperties)gipa(0, "vkEnumerateInstanceExtensionProperties");
    uint32_t n = 0; eie(0, &n, 0); VkExtensionProperties *ie = calloc(n ? n : 1, sizeof *ie); eie(0, &n, ie);
    P("extensões de instância (%u):", n); for (uint32_t i = 0; i < n; i++) P(" %s", ie[i].extensionName); P("\n");
    int has_display = 0, has_surface = 0;
    for (uint32_t i = 0; i < n; i++) { if (!strcmp(ie[i].extensionName, "VK_KHR_display")) has_display = 1; if (!strcmp(ie[i].extensionName, "VK_KHR_surface")) has_surface = 1; }

    PFN_vkCreateInstance ci = (PFN_vkCreateInstance)gipa(0, "vkCreateInstance");
    VkApplicationInfo ai = {VK_STRUCTURE_TYPE_APPLICATION_INFO, 0, "vkprobe", 1, "none", 1, VK_API_VERSION_1_0};
    VkInstanceCreateInfo ic = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, 0, 0, &ai};
    VkInstance inst; VkResult r = ci(&ic, 0, &inst);
    P("vkCreateInstance: %d\n", (int)r);
    if (r != VK_SUCCESS) { P("RESULTADO: não consegue criar instância Vulkan.\n"); return 1; }

    PFN_vkEnumeratePhysicalDevices epd = (PFN_vkEnumeratePhysicalDevices)gipa(inst, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceProperties gpp = (PFN_vkGetPhysicalDeviceProperties)gipa(inst, "vkGetPhysicalDeviceProperties");
    PFN_vkGetPhysicalDeviceFeatures gpf = (PFN_vkGetPhysicalDeviceFeatures)gipa(inst, "vkGetPhysicalDeviceFeatures");
    PFN_vkEnumerateDeviceExtensionProperties edep = (PFN_vkEnumerateDeviceExtensionProperties)gipa(inst, "vkEnumerateDeviceExtensionProperties");
    uint32_t nd = 0; epd(inst, &nd, 0);
    P("dispositivos físicos: %u\n", nd);
    VkPhysicalDevice *pd = calloc(nd ? nd : 1, sizeof *pd); epd(inst, &nd, pd);
    for (uint32_t d = 0; d < nd; d++) {
        VkPhysicalDeviceProperties pr; gpp(pd[d], &pr);
        P("\n[GPU %u] %s  tipo=%d  vendor=0x%x  driver=0x%x\n", d, pr.deviceName, (int)pr.deviceType, pr.vendorID, pr.driverVersion);
        P("  apiVersion=%u.%u.%u  maxImageDimension2D=%u  maxPushConstants=%u  maxBoundDescSets=%u\n",
          VK_VERSION_MAJOR(pr.apiVersion), VK_VERSION_MINOR(pr.apiVersion), VK_VERSION_PATCH(pr.apiVersion),
          pr.limits.maxImageDimension2D, pr.limits.maxPushConstantsSize, pr.limits.maxBoundDescriptorSets);
        VkPhysicalDeviceFeatures f; gpf(pd[d], &f);
        P("  dualSrcBlend=%u independentBlend=%u geometryShader=%u textureCompressionETC2=%u ASTC_LDR=%u samplerAnisotropy=%u\n",
          f.dualSrcBlend, f.independentBlend, f.geometryShader, f.textureCompressionETC2, f.textureCompressionASTC_LDR, f.samplerAnisotropy);
        uint32_t ne = 0; edep(pd[d], 0, &ne, 0); VkExtensionProperties *de = calloc(ne ? ne : 1, sizeof *de); edep(pd[d], 0, &ne, de);
        P("  extensões de dispositivo (%u):", ne); for (uint32_t i = 0; i < ne; i++) P(" %s", de[i].extensionName); P("\n");
        const char *need[] = {"VK_KHR_swapchain", "VK_KHR_dynamic_rendering", "VK_KHR_push_descriptor", "VK_KHR_create_renderpass2", "VK_KHR_depth_stencil_resolve", 0};
        for (int i = 0; need[i]; i++) { int ok = 0; for (uint32_t j = 0; j < ne; j++) if (!strcmp(de[j].extensionName, need[i])) ok = 1; P("  exige %-32s %s\n", need[i], ok ? "SIM" : "não");}
    }
    P("\nresumo de tela: VK_KHR_surface=%s VK_KHR_display=%s (sem janela X11/Wayland, o KMSDRM precisa de VK_KHR_display ou integração com o gbm)\n", has_surface ? "sim" : "não", has_display ? "sim" : "não");
    P("arquivo gravado em: %s\n", path);
    return 0;
}
