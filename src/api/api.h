#ifndef HABITS_API_API_H
#define HABITS_API_API_H

#include <stdbool.h>

#include "core/model.h"
#include "infra/http.h"

typedef struct {
    DB *db;
    const char *data_dir;
    const char *web_dir; /* NULL: serve the embedded bundle */
    const char *version;
    bool verbose;
} Server;

void api_handle(const HttpRequest *req, HttpResponse *res, void *ud);

#endif
