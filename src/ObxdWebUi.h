#pragma once

// The web editor's files (webui/), compiled into the module by
// cmake/EmbedWebUi.cmake so the editor works whatever subset of the bundle a
// host mounts. clap.webview/3 get_resource() serves them by absolute path.

#include <cstddef>

namespace obxd {

struct WebUiResource {
    const char *path; // "/index.html"
    const char *mime;
    const unsigned char *data;
    size_t size;
};

const WebUiResource *findWebUiResource(const char *path) noexcept;

} // namespace obxd
