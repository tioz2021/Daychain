#ifndef HABITS_INFRA_WEB_H
#define HABITS_INFRA_WEB_H

#include <stddef.h>

typedef struct {
    const char *path; /* "/index.html" */
    const char *mime;
    const unsigned char *data;
    size_t len;
} WebAsset;

/* Provided by the generated bundle. */
extern const WebAsset web_assets[];
extern const unsigned web_assets_count;

const WebAsset *web_asset_find(const char *path);
const char *web_mime_for(const char *path);

#endif
