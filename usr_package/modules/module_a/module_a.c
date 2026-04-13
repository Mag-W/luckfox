#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include "module.h"
#include "log.h"

static int g_running = 0;
static pthread_t g_thread_id;

static void* module_a_thread(void *arg) {
    LOG_INFO_FMT("Module_A thread started");
    
    while (g_running) {
        LOG_DEBUG_FMT("Module_A working...");
        sleep(1);
    }
    
    return NULL;
}

static int module_a_init(void) {
    LOG_INFO_FMT("Module_A: Init phase");
    /* 资源分配、初始化 */
    return 0;
}

static int module_a_start(void) {
    LOG_INFO_FMT("Module_A: Start phase");
    g_running = 1;
    
    if (pthread_create(&g_thread_id, NULL, module_a_thread, NULL) != 0) {
        LOG_ERROR_FMT("Failed to create thread");
        return -1;
    }
    
    return 0;
}

static int module_a_stop(void) {
    LOG_INFO_FMT("Module_A: Stop phase");
    g_running = 0;
    pthread_join(g_thread_id, NULL);
    return 0;
}

static int module_a_health_check(void) {
    return g_running ? 0 : -1;
}

/* 模块声明 */
static module_t module_a = {
    .name = "Module_A",
    .version = "1.0.0",
    .author = "Team",
    .init = module_a_init,
    .start = module_a_start,
    .stop = module_a_stop,
    .health_check = module_a_health_check
};

/* 模块自动注册 */
__attribute__((constructor))
void module_a_register(void) {
    module_register(&module_a);
}