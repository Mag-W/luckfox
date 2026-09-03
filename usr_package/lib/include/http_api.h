#ifndef HTTP_API_H
#define HTTP_API_H

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 业务模块注册的"接口回调"。
 *
 * params : 请求里的 params 对象（JSON，可能为 NULL）
 * result : 回调把响应数据填到这个 JSON 对象中，
 *          框架会把它封装进应答报文的 "data" 字段。
 *
 * 返回值：0 表示成功；非 0 表示失败（应答 code 会变为非 0）。
 */
typedef int (*api_handler_t)(const cJSON *params, cJSON *result);

/* 注册一个接口。method 形如 "video.get_status"、"system.ping"。
 * 返回 0 成功，-1 失败（重复注册 / 参数非法）。 */
int http_api_register(const char *method, api_handler_t handler);

/* 注销接口。返回 0 成功，-1 未找到。 */
int http_api_unregister(const char *method);

/* HTTP 服务器内部使用：按 method 查表并调用回调。 */
int http_api_dispatch(const char *method, const cJSON *params, cJSON *result);

/* 注册内置调试接口：system.ping / system.list / demo.echo（幂等，
 * 可多次调用）。建议在 http_sever init 时调用一次。 */
int http_api_init(void);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_API_H */