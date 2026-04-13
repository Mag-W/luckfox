#include "module.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

/* 全局模块链表头节点 */
static module_t g_module_node = {
    .name = NULL,
    .version = NULL,
    .author = NULL,
    .next = NULL,
    .state = MODULE_STATE_UNREGISTERED,
    .init_time_ms = 0,
    .start_time_ms = 0,
    .private_data = NULL
};

static int g_module_count = 0;
static pthread_mutex_t g_module_lock = PTHREAD_MUTEX_INITIALIZER;

/**
 * 获取链表头节点
 */
module_t* module_get_head(void) {
    return &g_module_node;
}

/**
 * 注册模块到全局链表
 */
int module_register(module_t *module) {
    if (module == NULL || module->name == NULL) {
        fprintf(stderr, "[MODULE_REGISTRY] ERROR: Invalid module pointer\n");
        return -1;
    }
    
    pthread_mutex_lock(&g_module_lock);
    
    /* 检查是否已注册 */
    module_t *curr = g_module_node.next;
    while (curr != NULL) {
        if (strcmp(curr->name, module->name) == 0) {
            fprintf(stderr, "[MODULE_REGISTRY] ERROR: Module '%s' already registered\n", 
                    module->name);
            pthread_mutex_unlock(&g_module_lock);
            return -1;
        }
        curr = curr->next;
    }
    
    /* 初始化模块节点 */
    module->next = NULL;
    module->state = MODULE_STATE_UNREGISTERED;
    module->init_time_ms = 0;
    module->start_time_ms = 0;
    module->private_data = NULL;
    
    /* 添加到链表尾部 */
    module_t *tail = &g_module_node;
    while (tail->next != NULL) {
        tail = tail->next;
    }
    tail->next = module;
    
    g_module_count++;
    module->state = MODULE_STATE_REGISTERED;
    printf("[MODULE_REGISTRY] ✓ Module '%s' registered (total: %d)\n", 
           module->name, g_module_count);
    
    pthread_mutex_unlock(&g_module_lock);
    return 0;
}

/**
 * 取消注册模块
 */
int module_unregister(const char *name) {
    if (name == NULL) {
        return -1;
    }
    
    pthread_mutex_lock(&g_module_lock);
    
    module_t *prev = &g_module_node;
    module_t *curr = g_module_node.next;
    
    while (curr != NULL) {
        if (strcmp(curr->name, name) == 0) {
            prev->next = curr->next;
            g_module_count--;
            printf("[MODULE_REGISTRY] ✓ Module '%s' unregistered\n", name);
            pthread_mutex_unlock(&g_module_lock);
            return 0;
        }
        prev = curr;
        curr = curr->next;
    }
    
    printf("[MODULE_REGISTRY] WARNING: Module '%s' not found\n", name);
    pthread_mutex_unlock(&g_module_lock);
    return -1;
}

/**
 * 获取已注册的模块
 */
module_t* module_get(const char *name) {
    if (name == NULL) {
        return NULL;
    }
    
    pthread_mutex_lock(&g_module_lock);
    
    module_t *curr = g_module_node.next;
    while (curr != NULL) {
        if (strcmp(curr->name, name) == 0) {
            pthread_mutex_unlock(&g_module_lock);
            return curr;
        }
        curr = curr->next;
    }
    
    pthread_mutex_unlock(&g_module_lock);
    return NULL;
}

/**
 * 获取注册的模块数量
 */
int module_get_count(void) {
    return g_module_count;
}

/**
 * 打印所有已注册模块信息
 */
void module_print_all(void) {
    pthread_mutex_lock(&g_module_lock);
    
    printf("\n╔════════════════════════════════════════════╗\n");
    printf("║        Registered Modules List            ║\n");
    printf("╠════════════════════════════════════════════╣\n");
    
    if (g_module_node.next == NULL) {
        printf("║ (no modules registered)                   ║\n");
    } else {
        module_t *curr = g_module_node.next;
        int idx = 1;
        while (curr != NULL) {
            printf("║ [%d] %-35s ║\n", idx, curr->name);
            printf("║     Ver: %-30s ║\n", curr->version ? curr->version : "N/A");
            printf("║     Author: %-25s ║\n", curr->author ? curr->author : "N/A");
            printf("║     State: ");
            
            switch (curr->state) {
                case MODULE_STATE_UNREGISTERED: printf("UNREGISTERED");     break;
                case MODULE_STATE_REGISTERED:   printf("REGISTERED");       break;
                case MODULE_STATE_INIT:         printf("INITIALIZED");     break;
                case MODULE_STATE_RUNNING:      printf("RUNNING");         break;
                case MODULE_STATE_STOPPED:      printf("STOPPED");         break;
                case MODULE_STATE_ERROR:        printf("ERROR");           break;
                default:                        printf("UNKNOWN");         break;
            }
            printf("                    ║\n");
            
            curr = curr->next;
            idx++;
        }
    }
    
    printf("╠════════════════════════════════════════════╣\n");
    printf("║ Total: %d module(s)                         ║\n", g_module_count);
    printf("╚════════════════════════════════════════════╝\n\n");
    
    pthread_mutex_unlock(&g_module_lock);
}