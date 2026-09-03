#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "module.h"
#include "module_manager.h"
#include "log.h"

static uint32_t get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
}

/**
 * 初始化所有模块（按链表顺序）
 */
int module_manager_init_all(void) {
    LOG_INFO_FMT("====== Module Initialization Phase ======");
    
    module_t *head = module_get_head();
    int count = module_get_count();
    int idx = 1;
    
    if (count == 0) {
        LOG_WARN_FMT("No modules registered");
        return 0;
    }
    
    /* 直接遍历链表，按顺序初始化 */
    for (module_t *mod = head->next; mod != NULL; mod = mod->next) {
        uint32_t start_time = get_time_ms();
        
        LOG_INFO_FMT("Initializing [%d/%d] %s...", idx, count, mod->name);
        
        if (mod->init != NULL) {
            if (mod->init() != 0) {
                LOG_ERROR_FMT("Failed to init %s", mod->name);
                mod->state = MODULE_STATE_ERROR;
                /* 单个模块 init 失败不终止其他模块，避免一处坏拖垮整个进程 */
                idx++;
                continue;
            }
        }
        
        mod->state = MODULE_STATE_INIT;
        mod->init_time_ms = get_time_ms() - start_time;
        LOG_INFO_FMT("%s initialized in %u ms", mod->name, mod->init_time_ms);
        
        idx++;
    }
    
    LOG_INFO_FMT("========== Initialization Done ==========\n");
    
    return 0;
}

/**
 * 启动所有模块（按链表顺序）
 */
int module_manager_start_all(void) {
    LOG_INFO_FMT("======== Module Startup Phase ========");
    
    module_t *head = module_get_head();
    int count = module_get_count();
    int idx = 1;
    
    if (count == 0) {
        LOG_WARN_FMT("No modules to start");
        return 0;
    }
    
    /* 直接遍历链表，按顺序启动 */
    for (module_t *mod = head->next; mod != NULL; mod = mod->next) {
        uint32_t start_time = get_time_ms();
        
        LOG_INFO_FMT("Starting [%d/%d] %s...", idx, count, mod->name);
        
        if (mod->state == MODULE_STATE_ERROR) {
            LOG_WARN_FMT("Skipping %s (state: ERROR)", mod->name);
            idx++;
            continue;
        }
        
        if (mod->start != NULL) {
            if (mod->start() != 0) {
                LOG_ERROR_FMT("Failed to start %s", mod->name);
                mod->state = MODULE_STATE_ERROR;
                /* 单个模块 start 失败不终止其他模块 */
                idx++;
                continue;
            }
        }
        
        mod->state = MODULE_STATE_RUNNING;
        mod->start_time_ms = get_time_ms() - start_time;
        LOG_INFO_FMT("%s started in %u ms", mod->name, mod->start_time_ms);
        
        idx++;
    }
    
    LOG_INFO_FMT("=========== Startup Done ===========\n");
    
    return 0;
}

/**
 * 停止所有模块（反向顺序）
 */
int module_manager_stop_all(void) {
    LOG_INFO_FMT("======== Module Shutdown Phase ========");
    
    module_t *head = module_get_head();
    
    /* 找到链表尾部 */
    module_t *tail = NULL;
    for (module_t *mod = head->next; mod != NULL; mod = mod->next) {
        tail = mod;
    }
    
    /* 从尾部开始反向停止 */
    while (tail != NULL) {
        if (tail->state == MODULE_STATE_RUNNING) {
            LOG_INFO_FMT("Stopping %s...", tail->name);
            
            if (tail->stop != NULL) {
                if (tail->stop() != 0) {
                    LOG_WARN_FMT("Error stopping %s", tail->name);
                }
            }
            
            tail->state = MODULE_STATE_STOPPED;
        }
        
        /* 找上一个节点 */
        module_t *prev = NULL;
        for (module_t *mod = head->next; mod != NULL && mod->next != tail; mod = mod->next) {
            prev = mod;
        }
        tail = prev;
    }
    
    LOG_INFO_FMT("=========== Shutdown Done ===========\n");
    
    return 0;
}

/**
 * 健康检查
 */
int module_manager_health_check(void) {
    module_t *head = module_get_head();
    int unhealthy = 0;
    
    for (module_t *mod = head->next; mod != NULL; mod = mod->next) {
        if (mod->state == MODULE_STATE_RUNNING && mod->health_check != NULL) {
            if (mod->health_check() != 0) {
                LOG_WARN_FMT("%s health check failed", mod->name);
                unhealthy++;
            }
        }
    }
    
    return unhealthy > 0 ? -1 : 0;
}