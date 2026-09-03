#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "http_api.h"

/* ===================== 接口注册表 ===================== */

typedef struct http_api_entry_s {
    char method[128];
    api_handler_t handler;
    struct http_api_entry_s *next;
} http_api_entry_t;

static http_api_entry_t *g_head = NULL;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

/* ===================== 内置示例接口 ===================== */

static int api_system_ping(const cJSON *params, cJSON *result) {
    (void)params;
    cJSON_AddStringToObject(result, "reply", "pong");
    return 0;
}

static int api_system_list(const cJSON *params, cJSON *result) {
    (void)params;
    cJSON *arr = cJSON_CreateArray();
    if (!arr) return -1;

    pthread_mutex_lock(&g_lock);
    for (http_api_entry_t *e = g_head; e; e = e->next) {
        cJSON_AddItemToArray(arr, cJSON_CreateString(e->method));
    }
    pthread_mutex_unlock(&g_lock);

    cJSON_AddItemToObject(result, "methods", arr);
    return 0;
}

static int api_demo_echo(const cJSON *params, cJSON *result) {
    cJSON *p = params ? cJSON_Duplicate(params, 1) : cJSON_CreateObject();
    if (!p) return -1;
    cJSON_AddItemToObject(result, "echo", p);
    return 0;
}

/* ===================== 对外接口 ===================== */

int http_api_init(void) {
    static int done = 0;
    if (done) return 0;
    done = 1;

    http_api_register("system.ping", api_system_ping);
    http_api_register("system.list", api_system_list);
    http_api_register("demo.echo",   api_demo_echo);
    return 0;
}

int http_api_register(const char *method, api_handler_t handler) {
    if (!method || !handler) return -1;

    pthread_mutex_lock(&g_lock);

    /* 重复注册检查 */
    for (http_api_entry_t *e = g_head; e; e = e->next) {
        if (strcmp(e->method, method) == 0) {
            pthread_mutex_unlock(&g_lock);
            return -1;
        }
    }

    http_api_entry_t *e = calloc(1, sizeof(*e));
    if (!e) {
        pthread_mutex_unlock(&g_lock);
        return -1;
    }

    snprintf(e->method, sizeof(e->method), "%s", method);
    e->handler = handler;
    e->next = g_head;
    g_head = e;

    pthread_mutex_unlock(&g_lock);
    return 0;
}

int http_api_unregister(const char *method) {
    if (!method) return -1;

    pthread_mutex_lock(&g_lock);
    http_api_entry_t **pp = &g_head;
    while (*pp) {
        http_api_entry_t *e = *pp;
        if (strcmp(e->method, method) == 0) {
            *pp = e->next;
            free(e);
            pthread_mutex_unlock(&g_lock);
            return 0;
        }
        pp = &e->next;
    }
    pthread_mutex_unlock(&g_lock);
    return -1;
}

int http_api_dispatch(const char *method, const cJSON *params, cJSON *result) {
    if (!method || !result) return -1;

    api_handler_t handler = NULL;
    pthread_mutex_lock(&g_lock);
    for (http_api_entry_t *e = g_head; e; e = e->next) {
        if (strcmp(e->method, method) == 0) {
            handler = e->handler;
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);

    if (!handler) return -1;
    return handler(params, result);
}