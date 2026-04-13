#ifndef MODULE_H
#define MODULE_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    MODULE_STATE_UNREGISTERED = -1,
    MODULE_STATE_REGISTERED = 0,
    MODULE_STATE_INIT,
    MODULE_STATE_RUNNING,
    MODULE_STATE_STOPPED,
    MODULE_STATE_ERROR
} module_state_t;

typedef struct module_s {
    const char *name;
    const char *version;
    const char *author;
    
    int (*init)(void);
    int (*start)(void);
    int (*stop)(void);
    int (*health_check)(void);
    
    struct module_s *next;
    
    module_state_t state;
    uint32_t init_time_ms;
    uint32_t start_time_ms;
    void *private_data;
    
} module_t;

/* 由 module_manager/ 提供的接口 */
module_t* module_get_head(void);
int module_register(module_t *module);
int module_unregister(const char *name);
module_t* module_get(const char *name);
int module_get_count(void);
void module_print_all(void);

#endif /* MODULE_H */