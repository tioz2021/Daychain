#include "infra/web.h"

#include <string.h>

const WebAsset *web_asset_find(const char *path) {
    for (unsigned i = 0; i < web_assets_count; i++)
        if (strcmp(web_assets[i].path, path) == 0) return &web_assets[i];
    return NULL;
}

const char *web_mime_for(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (strcmp(dot, ".html") == 0) return "text/html; charset=utf-8";
    if (strcmp(dot, ".css") == 0) return "text/css; charset=utf-8";
    if (strcmp(dot, ".js") == 0) return "text/javascript; charset=utf-8";
    if (strcmp(dot, ".json") == 0) return "application/json; charset=utf-8";
    if (strcmp(dot, ".svg") == 0) return "image/svg+xml";
    if (strcmp(dot, ".png") == 0) return "image/png";
    if (strcmp(dot, ".ico") == 0) return "image/x-icon";
    if (strcmp(dot, ".woff2") == 0) return "font/woff2";
    return "application/octet-stream";
}
