#define _DEFAULT_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "log.h"
#include "module.h"

#define PT_LISTEN_PORT           19000
#define PT_BACKLOG               4
#define PT_IO_TIMEOUT_MS         5000
#define PT_MAX_PAYLOAD_LEN       2048
#define PT_MAX_FRAME_LEN         (8 + PT_MAX_PAYLOAD_LEN)

#define PT_CODE_OK               0
#define PT_CODE_BAD_PACKET       400
#define PT_CODE_UNKNOWN_CMD      404
#define PT_CODE_INTERNAL_ERR     500
#define PT_CODE_TIMEOUT          408

#define PT_CMD_ECHO              1
#define PT_CMD_GET_STATUS        2

typedef struct {
    uint32_t request_id;
    uint16_t cmd;
    uint16_t payload_len;
    const uint8_t *payload;
} passthrough_request_t;

typedef struct {
    uint32_t request_id;
    uint16_t cmd;
    uint16_t code;
    uint16_t payload_len;
    uint8_t payload[PT_MAX_PAYLOAD_LEN];
} passthrough_response_t;

static volatile int g_running = 0;
static int g_listen_fd = -1;
static pthread_t g_server_tid;

static void close_fd_safe(int *fd) {
    if (fd && *fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

static int set_socket_timeout(int fd, int timeout_ms) {
    struct timeval tv;

    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0)
        return -1;
    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) != 0)
        return -1;

    return 0;
}

static int is_timeout_errno(int err) {
    return (err == EAGAIN || err == EWOULDBLOCK || err == ETIMEDOUT);
}

static int readn(int fd, void *buf, size_t n) {
    uint8_t *p = (uint8_t *)buf;
    size_t total = 0;

    while (total < n) {
        ssize_t r = recv(fd, p + total, n - total, 0);
        if (r == 0)
            return 0;
        if (r < 0) {
            if (errno == EINTR)
                continue;
            if (is_timeout_errno(errno))
                return -2;
            return -1;
        }
        total += (size_t)r;
    }

    return 1;
}

static int writen(int fd, const void *buf, size_t n) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t total = 0;

    while (total < n) {
        ssize_t w = send(fd, p + total, n - total, 0);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            if (is_timeout_errno(errno))
                return -2;
            return -1;
        }
        if (w == 0)
            return -1;
        total += (size_t)w;
    }

    return 0;
}

static uint16_t status_payload(uint8_t *out, uint16_t out_max) {
    time_t now = time(NULL);
    int n = snprintf((char *)out, out_max,
                     "{\"status\":\"ok\",\"module\":\"passthrough\",\"ts\":%ld}",
                     (long)now);
    if (n < 0)
        return 0;
    if ((size_t)n >= out_max)
        return (uint16_t)(out_max - 1);
    return (uint16_t)n;
}

static void dispatch_command(const passthrough_request_t *req, passthrough_response_t *resp) {
    resp->request_id = req->request_id;
    resp->cmd = req->cmd;
    resp->code = PT_CODE_OK;
    resp->payload_len = 0;

    switch (req->cmd) {
    case PT_CMD_ECHO:
        resp->payload_len = req->payload_len;
        if (resp->payload_len > 0)
            memcpy(resp->payload, req->payload, resp->payload_len);
        break;
    case PT_CMD_GET_STATUS:
        resp->payload_len = status_payload(resp->payload, sizeof(resp->payload));
        break;
    default:
        resp->code = PT_CODE_UNKNOWN_CMD;
        resp->payload_len = (uint16_t)snprintf((char *)resp->payload, sizeof(resp->payload),
                                               "unknown cmd: %u", req->cmd);
        if (resp->payload_len >= sizeof(resp->payload))
            resp->payload_len = sizeof(resp->payload) - 1;
        break;
    }
}

static int send_response(int fd, const passthrough_response_t *resp) {
    uint32_t frame_len_n;
    uint32_t req_id_n;
    uint16_t cmd_n;
    uint16_t code_n;
    uint16_t payload_len_n;

    uint32_t frame_len = (uint32_t)(10 + resp->payload_len);
    frame_len_n = htonl(frame_len);
    req_id_n = htonl(resp->request_id);
    cmd_n = htons(resp->cmd);
    code_n = htons(resp->code);
    payload_len_n = htons(resp->payload_len);

    if (writen(fd, &frame_len_n, sizeof(frame_len_n)) != 0)
        return -1;
    if (writen(fd, &req_id_n, sizeof(req_id_n)) != 0)
        return -1;
    if (writen(fd, &cmd_n, sizeof(cmd_n)) != 0)
        return -1;
    if (writen(fd, &code_n, sizeof(code_n)) != 0)
        return -1;
    if (writen(fd, &payload_len_n, sizeof(payload_len_n)) != 0)
        return -1;
    if (resp->payload_len > 0 && writen(fd, resp->payload, resp->payload_len) != 0)
        return -1;

    return 0;
}

static int send_error_response(int fd, uint32_t request_id, uint16_t cmd, uint16_t code, const char *msg) {
    passthrough_response_t resp;
    memset(&resp, 0, sizeof(resp));
    resp.request_id = request_id;
    resp.cmd = cmd;
    resp.code = code;

    if (msg) {
        resp.payload_len = (uint16_t)snprintf((char *)resp.payload, sizeof(resp.payload), "%s", msg);
        if (resp.payload_len >= sizeof(resp.payload))
            resp.payload_len = sizeof(resp.payload) - 1;
    }

    return send_response(fd, &resp);
}

static int handle_passthrough_request(int client_fd) {
    uint32_t frame_len_n = 0;
    uint32_t frame_len = 0;
    uint8_t frame_buf[PT_MAX_FRAME_LEN];
    passthrough_request_t req;
    passthrough_response_t resp;
    int rc;

    rc = readn(client_fd, &frame_len_n, sizeof(frame_len_n));
    if (rc <= 0)
        return rc;

    frame_len = ntohl(frame_len_n);
    if (frame_len < 8 || frame_len > PT_MAX_FRAME_LEN) {
        LOG_WARN_FMT("passthrough invalid frame_len=%u", frame_len);
        (void)send_error_response(client_fd, 0, 0, PT_CODE_BAD_PACKET, "invalid frame length");
        return -1;
    }

    rc = readn(client_fd, frame_buf, frame_len);
    if (rc <= 0) {
        if (rc == -2) {
            (void)send_error_response(client_fd, 0, 0, PT_CODE_TIMEOUT, "read timeout");
        }
        return -1;
    }

    {
        uint32_t request_id_n;
        uint16_t cmd_n;
        uint16_t payload_len_n;
        memcpy(&request_id_n, frame_buf + 0, sizeof(request_id_n));
        memcpy(&cmd_n, frame_buf + 4, sizeof(cmd_n));
        memcpy(&payload_len_n, frame_buf + 6, sizeof(payload_len_n));
        req.request_id = ntohl(request_id_n);
        req.cmd = ntohs(cmd_n);
        req.payload_len = ntohs(payload_len_n);
    }

    if ((uint32_t)(8 + req.payload_len) != frame_len || req.payload_len > PT_MAX_PAYLOAD_LEN) {
        LOG_WARN_FMT("passthrough malformed packet req_id=%u cmd=%u payload_len=%u frame_len=%u",
                     req.request_id, req.cmd, req.payload_len, frame_len);
        (void)send_error_response(client_fd, req.request_id, req.cmd, PT_CODE_BAD_PACKET,
                                  "payload length mismatch");
        return -1;
    }

    req.payload = frame_buf + 8;

    dispatch_command(&req, &resp);
    if (send_response(client_fd, &resp) != 0) {
        LOG_WARN_FMT("passthrough send response failed req_id=%u", req.request_id);
        return -1;
    }

    return 1;
}

static void *passthrough_server_thread(void *arg) {
    (void)arg;

    while (g_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(g_listen_fd, (struct sockaddr *)&client_addr, &client_len);

        if (client_fd < 0) {
            if (!g_running)
                break;
            if (errno == EINTR)
                continue;
            LOG_WARN_FMT("passthrough accept failed: %s", strerror(errno));
            usleep(50 * 1000);
            continue;
        }

        if (set_socket_timeout(client_fd, PT_IO_TIMEOUT_MS) != 0) {
            LOG_WARN_FMT("passthrough set timeout failed");
            close(client_fd);
            continue;
        }

        LOG_INFO_FMT("passthrough client connected: %s:%d",
                     inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        while (g_running) {
            int hret = handle_passthrough_request(client_fd);
            if (hret <= 0)
                break;
        }

        close(client_fd);
        LOG_INFO_FMT("passthrough client disconnected");
    }

    return NULL;
}

static int passthrough_module_init(void) {
    LOG_INFO_FMT("passthrough module init");
    return 0;
}

static int passthrough_module_start(void) {
    struct sockaddr_in addr;
    int on = 1;

    if (g_running)
        return 0;

    g_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listen_fd < 0) {
        LOG_ERROR_FMT("passthrough socket create failed: %s", strerror(errno));
        return -1;
    }

    setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PT_LISTEN_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(g_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        LOG_ERROR_FMT("passthrough bind %d failed: %s", PT_LISTEN_PORT, strerror(errno));
        close_fd_safe(&g_listen_fd);
        return -1;
    }

    if (listen(g_listen_fd, PT_BACKLOG) != 0) {
        LOG_ERROR_FMT("passthrough listen failed: %s", strerror(errno));
        close_fd_safe(&g_listen_fd);
        return -1;
    }

    g_running = 1;
    if (pthread_create(&g_server_tid, NULL, passthrough_server_thread, NULL) != 0) {
        LOG_ERROR_FMT("passthrough thread create failed");
        g_running = 0;
        close_fd_safe(&g_listen_fd);
        return -1;
    }

    LOG_INFO_FMT("passthrough server started on port %d", PT_LISTEN_PORT);
    return 0;
}

static int passthrough_module_stop(void) {
    if (!g_running)
        return 0;

    g_running = 0;
    if (g_listen_fd >= 0)
        shutdown(g_listen_fd, SHUT_RDWR);
    close_fd_safe(&g_listen_fd);
    pthread_join(g_server_tid, NULL);

    LOG_INFO_FMT("passthrough server stopped");
    return 0;
}

static int passthrough_module_health_check(void) {
    return g_running ? 0 : -1;
}

static module_t passthrough_module = {
    .name = "passthrough",
    .version = "1.0.0",
    .author = "Mag-W",
    .init = passthrough_module_init,
    .start = passthrough_module_start,
    .stop = passthrough_module_stop,
    .health_check = passthrough_module_health_check,
};

__attribute__((constructor))
void passthrough_module_register(void) {
    module_register(&passthrough_module);
}
