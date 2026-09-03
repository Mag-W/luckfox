#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "log.h"
#include "param.h"

dictionary *g_ini_d_ = NULL;

static char g_ini_path[256];
static pthread_mutex_t g_param_mutex = PTHREAD_MUTEX_INITIALIZER;

int rk_param_init(const char *ini_path) {
    pthread_mutex_lock(&g_param_mutex);

    if (g_ini_d_) {
        iniparser_freedict(g_ini_d_);
        g_ini_d_ = NULL;
    }

    snprintf(g_ini_path, sizeof(g_ini_path), "%s",
             ini_path ? ini_path : "/userdata/video.ini");

    g_ini_d_ = iniparser_load(g_ini_path);
    if (!g_ini_d_) {
        pthread_mutex_unlock(&g_param_mutex);
        LOG_ERROR_FMT("param: failed to load ini: %s\n", g_ini_path);
        return -1;
    }

    pthread_mutex_unlock(&g_param_mutex);
    return 0;
}

int rk_param_deinit(void) {
    pthread_mutex_lock(&g_param_mutex);

    if (g_ini_d_) {
        iniparser_freedict(g_ini_d_);
        g_ini_d_ = NULL;
    }

    pthread_mutex_unlock(&g_param_mutex);
    return 0;
}

int rk_param_reload(void) {
    dictionary *new_ini;

    pthread_mutex_lock(&g_param_mutex);

    new_ini = iniparser_load(g_ini_path);
    if (!new_ini) {
        pthread_mutex_unlock(&g_param_mutex);
        LOG_ERROR_FMT("param: failed to reload ini: %s\n", g_ini_path);
        return -1;
    }

    if (g_ini_d_) {
        iniparser_freedict(g_ini_d_);
    }
    g_ini_d_ = new_ini;

    pthread_mutex_unlock(&g_param_mutex);
    return 0;
}

int rk_param_save(void) {
    FILE *fp;

    pthread_mutex_lock(&g_param_mutex);

    if (!g_ini_d_) {
        pthread_mutex_unlock(&g_param_mutex);
        return -1;
    }

    fp = fopen(g_ini_path, "w");
    if (!fp) {
        pthread_mutex_unlock(&g_param_mutex);
        LOG_ERROR_FMT("param: failed to open ini for write: %s\n", g_ini_path);
        return -1;
    }

    iniparser_dump_ini(g_ini_d_, fp);
    fclose(fp);

    pthread_mutex_unlock(&g_param_mutex);
    return 0;
}

int rk_param_get_int(const char *entry, int default_val) {
    int value;

    pthread_mutex_lock(&g_param_mutex);
    value = g_ini_d_ ? iniparser_getint(g_ini_d_, entry, default_val) : default_val;
    pthread_mutex_unlock(&g_param_mutex);

    return value;
}

double rk_param_get_double(const char *entry, double default_val) {
    double value;

    pthread_mutex_lock(&g_param_mutex);
    value = g_ini_d_ ? iniparser_getdouble(g_ini_d_, entry, default_val) : default_val;
    pthread_mutex_unlock(&g_param_mutex);

    return value;
}

const char *rk_param_get_string(const char *entry, const char *default_val) {
    const char *value;

    pthread_mutex_lock(&g_param_mutex);
    value = g_ini_d_ ? iniparser_getstring(g_ini_d_, entry, default_val) : default_val;
    pthread_mutex_unlock(&g_param_mutex);

    return value;
}

int rk_param_set_int(const char *entry, int val) {
    char tmp[32];

    pthread_mutex_lock(&g_param_mutex);

    if (!g_ini_d_) {
        pthread_mutex_unlock(&g_param_mutex);
        return -1;
    }

    snprintf(tmp, sizeof(tmp), "%d", val);
    iniparser_set(g_ini_d_, entry, tmp);

    pthread_mutex_unlock(&g_param_mutex);
    return 0;
}

int rk_param_set_string(const char *entry, const char *val) {
    pthread_mutex_lock(&g_param_mutex);

    if (!g_ini_d_) {
        pthread_mutex_unlock(&g_param_mutex);
        return -1;
    }

    iniparser_set(g_ini_d_, entry, val);

    pthread_mutex_unlock(&g_param_mutex);
    return 0;
}
