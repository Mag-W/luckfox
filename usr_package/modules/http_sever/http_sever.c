#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include "module.h"
#include "log.h"
#include "http_api.h"
#include "cJSON.h"

/* ===================== 配置 ===================== */
#define HTTP_DEFAULT_PORT     8080
#define HTTP_BUF_SIZE         (16 * 1024)   /* 单请求最大 16KB */
#define HTTP_RECV_TIMEOUT_S   5             /* 单连接读超时 5s */
#define HTTP_MAX_BODY         (8 * 1024)    /* body 最大 8KB */

typedef struct {
    int port;
} http_sever_cfg_t;

static http_sever_cfg_t g_cfg = { .port = HTTP_DEFAULT_PORT };
static int g_listen_fd = -1;
static int g_running = 0;
static pthread_t g_accept_thread;
static pthread_mutex_t g_conn_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_conn_count = 0;
#define HTTP_MAX_CONN 16

/* ===================== HTTP 基础工具 ===================== */

static const char *http_status_text(int status) {
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";
        default:  return "Unknown";
    }
}

/* 把整段数据发完（避免 SIGPIPE 导致进程退出） */
static int http_send_all(int fd, const void *data, size_t len) {
    const char *p = (const char *)data;
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, p + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

/* 发送一个 HTTP/JSON 响应 */
static void http_send_json_raw(int fd, int status, const char *body) {
    char head[256];
    int hl = snprintf(head, sizeof(head),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n",
        status, http_status_text(status), (int)strlen(body));
    if (hl > 0) http_send_all(fd, head, (size_t)hl);
    http_send_all(fd, body, strlen(body));
}

/* ===================== 请求解析 ===================== */

typedef struct {
    char method[8];           /* GET / POST */
    char path[512];           /* /api 或 /api?method=... */
    int  content_length;      /* body 长度 */
    int  head_len;            /* 头部长度(含 \r\n\r\n) */
} http_request_t;

/* 小写化比较：解析 Header 字段名 */
static int http_header_is(const char *line, const char *name) {
    while (*name) {
        char c = *line++;
        if (c >= 'A' && c <= 'Z') c += ('a' - 'A');
        if (c != *name++) return 0;
    }
    return *line == ':' || *line == ' ';
}

/* 从缓冲区解析请求行 + Content-Length */
static int http_parse_head(const char *buf, http_request_t *req) {
    memset(req, 0, sizeof(*req));

    /* 请求行 */
    if (sscanf(buf, "%7s %511s HTTP/%*[^\r\n]", req->method, req->path) != 2)
        return -1;

    /* Header 区(请求行之后) */
    const char *p = strstr(buf, "\r\n");
    if (!p) return -1;
    p += 2;

    while (*p && p[0] != '\r' && p[1] != '\n') {
        const char *nl = strstr(p, "\r\n");
        if (!nl) break;

        size_t line_len = (size_t)(nl - p);
        char line[256];
        if (line_len >= sizeof(line)) line_len = sizeof(line) - 1;
        memcpy(line, p, line_len);
        line[line_len] = '\0';

        if (http_header_is(line, "Content-Length")) {
            const char *v = strchr(line, ':');
            if (v) req->content_length = atoi(v + 1);
        }
        p = nl + 2;
    }
    return 0;
}

/* 读完整 HTTP 请求（含 body），成功返回 0 */
static int http_read_request(int fd, char *buf, int size, http_request_t *req) {
    int total = 0;
    int need = -1;

    while (total < size - 1) {
        ssize_t n = recv(fd, buf + total, (size_t)(size - 1 - total), 0);
        if (n <= 0) break;
        total += (int)n;
        buf[total] = '\0';

        /* 找到头部结束标志 */
        char *head_end = strstr(buf, "\r\n\r\n");
        if (head_end) {
            if (http_parse_head(buf, req) != 0) return -1;
            req->head_len = (int)(head_end - buf) + 4;
            need = req->head_len + req->content_length;
            if (need > HTTP_MAX_BODY + 4096) return -1;  /* 过大 */
            break;
        }
    }

    if (need < 0 || total < req->head_len) return -1;

    /* 继续读 body 直到读完 */
    while (total < need && total < size - 1) {
        ssize_t n = recv(fd, buf + total, (size_t)(size - 1 - total), 0);
        if (n <= 0) break;
        total += (int)n;
    }
    buf[total] = '\0';
    return total >= need ? 0 : -1;
}
/* ===================== API 处理 ===================== */

static void http_process_api(int fd, const char *body) {
    cJSON *root = cJSON_Parse(body);
    if (!root) {
        http_send_json_raw(fd, 400, "{\"code\":400,\"msg\":\"invalid json\"}");
        return;
    }

    cJSON *m = cJSON_GetObjectItem(root, "method");
    if (!m || m->type != cJSON_String) {
        cJSON_Delete(root);
        http_send_json_raw(fd, 400, "{\"code\":400,\"msg\":\"missing method\"}");
        return;
    }

    cJSON *params = cJSON_GetObjectItem(root, "params");
    if (params && params->type != cJSON_Object) params = NULL;

    cJSON *result = cJSON_CreateObject();
    int rc = http_api_dispatch(m->valuestring, params, result);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "code", rc == 0 ? 0 : 404);
    cJSON_AddStringToObject(resp, "msg",  rc == 0 ? "ok" : "method not found");
    cJSON_AddItemToObject(resp, "data", result);

    cJSON *id = cJSON_GetObjectItem(root, "id");
    if (id) {
        cJSON *id_copy = cJSON_Duplicate(id, 1);
        if (id_copy) cJSON_AddItemToObject(resp, "id", id_copy);
    }

    char *out = cJSON_PrintUnformatted(resp);
    http_send_json_raw(fd, 200, out ? out : "{}");

    cJSON_free(out);
    cJSON_Delete(resp);
    cJSON_Delete(root);
}

/* 支持 GET /api?method=xxx（无 params） */
static void http_process_api_get(int fd, const char *path) {
    char method[128] = {0};
    const char *q = strchr(path, '?');
    if (q) {
        const char *p = strstr(q + 1, "method=");
        if (p) {
            p += 7;
            int i = 0;
            while (*p && *p != '&' && i < (int)sizeof(method) - 1)
                method[i++] = *p++;
        }
    }

    if (!method[0]) {
        http_send_json_raw(fd, 400, "{\"code\":400,\"msg\":\"missing method\"}");
        return;
    }

    cJSON *params = cJSON_CreateObject();
    cJSON *result = cJSON_CreateObject();
    int rc = http_api_dispatch(method, params, result);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "code", rc == 0 ? 0 : 404);
    cJSON_AddStringToObject(resp, "msg",  rc == 0 ? "ok" : "method not found");
    cJSON_AddItemToObject(resp, "data", result);

    char *out = cJSON_PrintUnformatted(resp);
    http_send_json_raw(fd, 200, out ? out : "{}");

    cJSON_free(out);
    cJSON_Delete(resp);
    cJSON_Delete(result);
    cJSON_Delete(params);
}

static void http_route_request(int fd, const http_request_t *req, const char *body) {
    /* GET /ping → 简单探活 */
    if (strcmp(req->method, "GET") == 0 && strcmp(req->path, "/ping") == 0) {
        http_send_json_raw(fd, 200, "{\"code\":0,\"msg\":\"pong\",\"data\":{}}");
        return;
    }

    /* 路由到 /api */
    if (strncmp(req->path, "/api", 4) != 0) {
        http_send_json_raw(fd, 404, "{\"code\":404,\"msg\":\"not found\"}");
        return;
    }

    if (strcmp(req->method, "POST") == 0) {
        http_process_api(fd, body);
        return;
    }
    if (strcmp(req->method, "GET") == 0) {
        http_process_api_get(fd, req->path);
        return;
    }
    http_send_json_raw(fd, 405, "{\"code\":405,\"msg\":\"method not allowed\"}");
}
/* ===================== 连接处理 ===================== */

static void *http_sever_client_thread(void *arg) {
    int fd = (int)(intptr_t)arg;
    char buf[HTTP_BUF_SIZE] = {0};
    http_request_t req;

    if (http_read_request(fd, buf, (int)sizeof(buf), &req) == 0) {
        const char *body = buf + req.head_len;
        http_route_request(fd, &req, body);
    } else {
        http_send_json_raw(fd, 400, "{\"code\":400,\"msg\":\"bad request\"}");
    }

    close(fd);
    pthread_mutex_lock(&g_conn_lock);
    g_conn_count--;
    pthread_mutex_unlock(&g_conn_lock);
    return NULL;
}

static void *http_sever_accept_thread(void *arg) {
    (void)arg;
    LOG_INFO_FMT("http_sever accepting connections on port %d", g_cfg.port);

    while (g_running) {
        struct sockaddr_in cli;
        socklen_t cli_len = sizeof(cli);
        int fd = accept(g_listen_fd, (struct sockaddr *)&cli, &cli_len);
        if (fd < 0) {
            if (!g_running) break;
            if (errno != EINTR) usleep(100 * 1000);
            continue;
        }

        pthread_mutex_lock(&g_conn_lock);
        if (g_conn_count >= HTTP_MAX_CONN) {
            pthread_mutex_unlock(&g_conn_lock);
            close(fd);
            continue;
        }
        g_conn_count++;
        pthread_mutex_unlock(&g_conn_lock);

        struct timeval tv = { .tv_sec = HTTP_RECV_TIMEOUT_S, .tv_usec = 0 };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        pthread_t t;
        if (pthread_create(&t, NULL, http_sever_client_thread, (void *)(intptr_t)fd) == 0) {
            pthread_detach(t);
        } else {
            close(fd);
            pthread_mutex_lock(&g_conn_lock);
            g_conn_count--;
            pthread_mutex_unlock(&g_conn_lock);
        }
    }
    return NULL;
}

/* ===================== 模块生命周期 ===================== */

static int http_sever_module_init(void) {
    LOG_INFO_FMT("http_sever module init");

    /* 注册内置接口(system.ping/system.list/demo.echo) */
    http_api_init();

    g_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listen_fd < 0) {
        LOG_ERROR_FMT("http_sever: socket() failed: %s", strerror(errno));
        return -1;
    }

    int opt = 1;
    setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    /* 尝试绑定端口，若被占用则自动顺延，避免冲突导致整个进程退出 */
    int bound = -1;
    int tried = 0;
    for (int p = g_cfg.port; p < g_cfg.port + 8 && tried < 8; p++, tried++) {
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons((uint16_t)p);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

        if (bind(g_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            bound = p;
            break;
        }
        LOG_WARN_FMT("http_sever: bind(port %d) failed, try next: %s",
                     p, strerror(errno));
    }

    if (bound < 0) {
        LOG_ERROR_FMT("http_sever: no free port in range %d..%d", g_cfg.port,
                      g_cfg.port + 9);
        close(g_listen_fd);
        g_listen_fd = -1;
        return -1;
    }

    LOG_INFO_FMT("http_sever: listening on port %d", bound);

    if (listen(g_listen_fd, 8) < 0) {
        LOG_ERROR_FMT("http_sever: listen() failed: %s", strerror(errno));
        close(g_listen_fd);
        g_listen_fd = -1;
        return -1;
    }
    return 0;
}

static int http_sever_module_start(void) {
    LOG_INFO_FMT("http_sever module start");
    g_running = 1;
    if (pthread_create(&g_accept_thread, NULL, http_sever_accept_thread, NULL) != 0) {
        LOG_ERROR_FMT("http_sever: failed to create accept thread");
        g_running = 0;
        return -1;
    }
    return 0;
}

static int http_sever_module_stop(void) {
    LOG_INFO_FMT("http_sever module stop");
    g_running = 0;

    if (g_listen_fd >= 0) {
        shutdown(g_listen_fd, SHUT_RDWR);
        close(g_listen_fd);
        g_listen_fd = -1;
    }
    if (g_accept_thread) {
        pthread_join(g_accept_thread, NULL);
        memset(&g_accept_thread, 0, sizeof(g_accept_thread));
    }
    return 0;
}

static int http_sever_module_health_check(void) {
    return (g_running && g_listen_fd >= 0) ? 0 : -1;
}

static module_t http_sever_module = {
    .name = "http_sever",
    .version = "1.0.0",
    .author = "Team",
    .init = http_sever_module_init,
    .start = http_sever_module_start,
    .stop = http_sever_module_stop,
    .health_check = http_sever_module_health_check,
};

__attribute__((constructor))
void http_sever_register(void) {
    module_register(&http_sever_module);
}