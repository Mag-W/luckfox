#ifndef MODULE_MANAGER_H
#define MODULE_MANAGER_H

/* 模块管理接口 */
int module_manager_init_all(void);
int module_manager_start_all(void);
int module_manager_stop_all(void);
int module_manager_health_check(void);

#endif /* MODULE_MANAGER_H */