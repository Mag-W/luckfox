#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include "module.h"
#include "log.h"

static int g_running = 0;
static pthread_t g_thread_id;

static void* module_b_thread(void *arg) {
    LOG_INFO_FMT("Module_B thread started");
    
    while (g_running) {
        LOG_DEBUG_FMT("Module_B working...");
        sleep(2);
    }
    
    return NULL;
}

static int module_b_init(void) {
    LOG_INFO_FMT("Module_B: Init phase");
    return 0;
}

static int module_b_start(void) {
    LOG_INFO_FMT("Module_B: Start phase");
    g_running = 1;
    
    if (pthread_create(&g_thread_id, NULL, module_b_thread, NULL) != 0) {
        LOG_ERROR_FMT("Failed to create thread");
        return -1;
    }
    
    return 0;
}

static int module_b_stop(void) {
    LOG_INFO_FMT("Module_B: Stop phase");
    g_running = 0;
    pthread_join(g_thread_id, NULL);
    return 0;
}

static int module_b_health_check(void) {
    return g_running ? 0 : -1;
}

static module_t module_b = {
    .name = "Module_B",
    .version = "1.0.0",
    .author = "Team",
    .init = module_b_init,
    .start = module_b_start,
    .stop = module_b_stop,
    .health_check = module_b_health_check
};

__attribute__((constructor))
void module_b_register(void) {
    module_register(&module_b);
}