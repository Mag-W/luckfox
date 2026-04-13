#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include "module.h"
#include "log.h"
#include "module_manager.h"

static int g_running = 1;

static void signal_handler(int sig) {
    printf("\n\n");
    LOG_INFO_FMT("Received signal %d, shutting down...", sig);
    g_running = 0;
}

int main(int argc, char *argv[]) {
    printf("\n╔════════════════════════════════════════╗\n");
    printf("║   Embedded Modular System - Main      ║\n");
    printf("╚════════════════════════════════════════╝\n\n");
    
    /* 初始化日志 */
#ifdef CONFIG_DEBUG_MODE
    log_init(LOG_DEBUG);
#else
    log_init(LOG_INFO);
#endif
    
    /* 注册信号处理 */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* 打印已注册模块 */
    module_print_all();
    
    /* 初始化所有模块 */
    if (module_manager_init_all() != 0) {
        LOG_ERROR_FMT("Module initialization failed");
        return -1;
    }
    
    /* 启动所有模块 */
    if (module_manager_start_all() != 0) {
        LOG_ERROR_FMT("Module startup failed");
        module_manager_stop_all();
        return -1;
    }
    
    /* 主循环 */
    LOG_INFO_FMT("System running...\n");
    
    while (g_running) {
        if (module_manager_health_check() != 0) {
            LOG_WARN_FMT("Some modules are unhealthy");
        }
        sleep(5);
    }
    
    /* 停止所有模块 */
    module_manager_stop_all();
    
    LOG_INFO_FMT("System shutdown completed");
    printf("\n");
    return 0;
}