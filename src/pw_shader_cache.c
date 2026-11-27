/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pw_shader_cache.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int directory(const char *path)
{
    struct stat st;
    if (mkdir(path, 0777) && errno != EEXIST) return errno;
    if (stat(path, &st)) return errno;
    if (!S_ISDIR(st.st_mode)) return ENOTDIR;
    if (access(path, W_OK)) return errno;
    return 0;
}

int pw_shader_cache_configure(const char *root, const char *profile,
                              const char *version_file, int enabled)
{
    char version[66], base[4096], path[4096], windows[4099], dxvk_windows[4099];
    int status = 0;
    FILE *file;
    /* Disable first, including the launcher's inherited RADV directory. */
    unsetenv("PS5_SHADER_CACHE_DIR");
    unsetenv("MESA_SHADER_CACHE_DIR");
    unsetenv("DXVK_SHADER_CACHE_PATH");
    unsetenv("VKD3D_SHADER_CACHE_PATH");
    unsetenv("DXVK_DXGI_ADAPTER_SUMMARY");
    if (setenv("MESA_SHADER_CACHE_DISABLE", "true", 1) || setenv("DXVK_SHADER_CACHE", "0", 1))
        return errno;
    if (!enabled) return 0;
    if (!profile || !*profile || !strcmp(profile, ".") || !strcmp(profile, "..") ||
        strpbrk(profile, "/\\")) return EINVAL;
    file = fopen(version_file, "r");
    if (!file) return errno;
    const int read = fgets(version, sizeof(version), file) != NULL;
    fclose(file);
    if (!read) return EINVAL;
    version[strcspn(version, "\r\n")] = 0;
    if (strlen(version) != 64 || strspn(version, "0123456789abcdef") != 64) return EINVAL;
    if ((status = directory(root))) return status;
    if (snprintf(base, sizeof(base), "%s/%s", root, profile) >= (int)sizeof(base)) return ENAMETOOLONG;
    if ((status = directory(base))) return status;
    if (snprintf(base, sizeof(base), "%s/%s/%s", root, profile, version) >= (int)sizeof(base)) return ENAMETOOLONG;
    if ((status = directory(base))) return status;
    static const char *const folders[] = {"radv", "gl", "dxvk", "vkd3d"};
    static const char *const variables[] = {"MESA_SHADER_CACHE_DIR", "PS5_SHADER_CACHE_DIR", "DXVK_SHADER_CACHE_PATH",
                                            "VKD3D_SHADER_CACHE_PATH"};
    dxvk_windows[0] = 0;
    for (unsigned i = 0; i < 4; ++i) {
        if (snprintf(path, sizeof(path), "%s/%s", base, folders[i]) >= (int)sizeof(path)) {
            status = ENAMETOOLONG;
            break;
        }
        if ((status = directory(path))) break;
        /* DXVK and VKD3D-Proton are Windows DLLs; the two native drivers consume POSIX paths. */
        snprintf(windows, sizeof(windows), "Z:%s", path);
        for (char *p = windows; *p; ++p) if (*p == '/') *p = '\\';
        if (i == 2) strcpy(dxvk_windows, windows);
        if (setenv(variables[i], i >= 2 ? windows : path, 1)) { status = errno; break; }
    }
    /* DXGI's adapters without Vulkan (platform/dxvk/patches/0004): the namespace changes with the driver and DXVK. */
    if (!status && strlen(dxvk_windows) + sizeof("\\dxgi-adapters.bin") > sizeof(dxvk_windows)) status = ENAMETOOLONG;
    if (!status && setenv("DXVK_DXGI_ADAPTER_SUMMARY", strcat(dxvk_windows, "\\dxgi-adapters.bin"), 1)) status = errno;
    if (!status && (setenv("MESA_SHADER_CACHE_DISABLE", "false", 1) || setenv("DXVK_SHADER_CACHE", "1", 1)))
        status = errno;
    if (status) pw_shader_cache_configure(root, profile, version_file, 0);
    return status;
}
