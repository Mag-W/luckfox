#ifndef USR_PARAM_H
#define USR_PARAM_H

#include "iniparser.h"

extern dictionary *g_ini_d_;

int rk_param_init(const char *ini_path);
int rk_param_deinit(void);
int rk_param_reload(void);
int rk_param_save(void);

int rk_param_get_int(const char *entry, int default_val);
double rk_param_get_double(const char *entry, double default_val);
const char *rk_param_get_string(const char *entry, const char *default_val);

int rk_param_set_int(const char *entry, int val);
int rk_param_set_string(const char *entry, const char *val);

#endif
