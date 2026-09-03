/*
 * rkipc_log.c
 *
 * 提供 rkipc 的 param.c 所依赖的两个全局符号。
 * 在原 rkipc 工程中这两个变量定义在 rv1106_ipc/main.c 里，
 * 这里我们只抽取了 param.c，因此单独补上定义即可通过链接。
 */
int enable_minilog = 0;
int rkipc_log_level = 0;
