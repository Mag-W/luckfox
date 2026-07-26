#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "module.h"
#include "log.h"
#include "param.h"

#include "rk_comm_sys.h"
#include "rk_comm_vi.h"
#include "rk_comm_venc.h"
#include "rk_mpi_sys.h"
#include "rk_mpi_vi.h"
#include "rk_mpi_venc.h"
#include "rk_mpi_mb.h"
#include "rtsp_demo.h"
#include "rkaiq/uAPI2/rk_aiq_user_api2_sysctl.h"

#define VIDEO_CONFIG_ENV      "VIDEO_CONFIG_PATH"
#define VIDEO_CONFIG_USERDATA "/userdata/video.ini"
#define VIDEO_CONFIG_OEM      "/oem/usr/etc/video.ini"
#define VIDEO_CONFIG_ETC      "/etc/video.ini"

typedef struct {
    int enable;

    int enable_aiq;
    char iq_file_dir[128];

    int vi_dev;
    int vi_pipe;
    int vi_chn;
    int vi_buf_count;

    int venc_chn;
    int width;
    int height;
    int max_width;
    int max_height;
    char codec[16];
    char h264_profile[16];
    char rc_mode[16];
    int fps;
    int gop;
    int bitrate_kbps;
    int venc_buf_count;
    int venc_buf_size;

    int rtsp_enable;
    int rtsp_port;
    char rtsp_path[64];
} video_config_t;

static video_config_t g_cfg;
static int g_running = 0;
static int g_started = 0;
static int g_param_ready = 0;
static pthread_t g_thread;

static rtsp_demo_handle g_rtsp_demo = NULL;
static rtsp_session_handle g_rtsp_session = NULL;
static pthread_mutex_t g_rtsp_lock = PTHREAD_MUTEX_INITIALIZER;

static rk_aiq_sys_ctx_t *g_aiq_ctx = NULL;
static int g_aiq_started = 0;

static RK_U32 align16(RK_U32 v) {
    return (v + 15) & ~15;
}

static void str_copy(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) {
        return;
    }

    snprintf(dst, dst_size, "%s", src ? src : "");
}

static void video_config_set_default(video_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));

    cfg->enable = 1;
    cfg->enable_aiq = 1;
    str_copy(cfg->iq_file_dir, sizeof(cfg->iq_file_dir), "/etc/iqfiles");

    cfg->vi_dev = 0;
    cfg->vi_pipe = 0;
    cfg->vi_chn = 0;
    cfg->vi_buf_count = 2;

    cfg->venc_chn = 0;
    cfg->width = 1920;
    cfg->height = 1080;
    cfg->max_width = 2560;
    cfg->max_height = 1440;
    str_copy(cfg->codec, sizeof(cfg->codec), "H.264");
    str_copy(cfg->h264_profile, sizeof(cfg->h264_profile), "high");
    str_copy(cfg->rc_mode, sizeof(cfg->rc_mode), "CBR");
    cfg->fps = 25;
    cfg->gop = 50;
    cfg->bitrate_kbps = 4096;
    cfg->venc_buf_count = 4;
    cfg->venc_buf_size = cfg->width * cfg->height;

    cfg->rtsp_enable = 1;
    cfg->rtsp_port = 8554;
    str_copy(cfg->rtsp_path, sizeof(cfg->rtsp_path), "/live/0");
}

static const char *video_config_find_path(void) {
    const char *env_path = getenv(VIDEO_CONFIG_ENV);

    if (env_path && access(env_path, R_OK) == 0) {
        return env_path;
    }
    if (access(VIDEO_CONFIG_USERDATA, R_OK) == 0) {
        return VIDEO_CONFIG_USERDATA;
    }
    if (access(VIDEO_CONFIG_OEM, R_OK) == 0) {
        return VIDEO_CONFIG_OEM;
    }
    if (access(VIDEO_CONFIG_ETC, R_OK) == 0) {
        return VIDEO_CONFIG_ETC;
    }

    return NULL;
}

static void video_config_load(video_config_t *cfg) {
    const char *config_path;
    const char *str;

    video_config_set_default(cfg);

    config_path = video_config_find_path();
    if (!config_path) {
        LOG_WARN_FMT("Video: config file not found, use default config\n");
        return;
    }

    if (rk_param_init(config_path) != 0) {
        LOG_WARN_FMT("Video: failed to load %s, use default config\n", config_path);
        return;
    }
    g_param_ready = 1;

    cfg->enable = rk_param_get_int("video.0:enable", cfg->enable);

    cfg->enable_aiq = rk_param_get_int("video.source:enable_aiq", cfg->enable_aiq);
    str = rk_param_get_string("video.source:iq_file_dir", cfg->iq_file_dir);
    str_copy(cfg->iq_file_dir, sizeof(cfg->iq_file_dir), str);

    cfg->vi_dev = rk_param_get_int("video.source:vi_dev", cfg->vi_dev);
    cfg->vi_pipe = rk_param_get_int("video.source:vi_pipe", cfg->vi_pipe);
    cfg->vi_chn = rk_param_get_int("video.source:vi_chn", cfg->vi_chn);
    cfg->vi_buf_count = rk_param_get_int("video.source:buf_count", cfg->vi_buf_count);

    cfg->venc_chn = rk_param_get_int("video.0:venc_chn", cfg->venc_chn);
    cfg->width = rk_param_get_int("video.0:width", cfg->width);
    cfg->height = rk_param_get_int("video.0:height", cfg->height);
    cfg->max_width = rk_param_get_int("video.0:max_width", cfg->max_width);
    cfg->max_height = rk_param_get_int("video.0:max_height", cfg->max_height);

    str = rk_param_get_string("video.0:output_data_type", cfg->codec);
    str_copy(cfg->codec, sizeof(cfg->codec), str);
    str = rk_param_get_string("video.0:h264_profile", cfg->h264_profile);
    str_copy(cfg->h264_profile, sizeof(cfg->h264_profile), str);
    str = rk_param_get_string("video.0:rc_mode", cfg->rc_mode);
    str_copy(cfg->rc_mode, sizeof(cfg->rc_mode), str);

    cfg->fps = rk_param_get_int("video.0:fps", cfg->fps);
    cfg->gop = rk_param_get_int("video.0:gop", cfg->gop);
    cfg->bitrate_kbps = rk_param_get_int("video.0:max_rate", cfg->bitrate_kbps);
    cfg->venc_buf_count = rk_param_get_int("video.0:buffer_count", cfg->venc_buf_count);
    cfg->venc_buf_size = rk_param_get_int("video.0:buffer_size", cfg->width * cfg->height);

    cfg->rtsp_enable = rk_param_get_int("rtsp:enable", cfg->rtsp_enable);
    cfg->rtsp_port = rk_param_get_int("rtsp:port", cfg->rtsp_port);
    str = rk_param_get_string("rtsp:path", cfg->rtsp_path);
    str_copy(cfg->rtsp_path, sizeof(cfg->rtsp_path), str);

    LOG_INFO_FMT("Video: config=%s %dx%d codec=%s rtsp=%d:%s\n",
                 config_path, cfg->width, cfg->height, cfg->codec,
                 cfg->rtsp_port, cfg->rtsp_path);
}

static int video_isp_init(void) {
    rk_aiq_static_info_t static_info;
    const char *sensor_name;

    if (!g_cfg.enable_aiq) {
        return 0;
    }

    memset(&static_info, 0, sizeof(static_info));
    setenv("HDR_MODE", "0", 1);

    if (rk_aiq_uapi2_sysctl_enumStaticMetasByPhyId(0, &static_info) != 0) {
        LOG_ERROR_FMT("Video: rk_aiq_uapi2_sysctl_enumStaticMetasByPhyId failed\n");
        return -1;
    }

    sensor_name = static_info.sensor_info.sensor_name;
    if (!sensor_name || sensor_name[0] == '\0') {
        LOG_ERROR_FMT("Video: empty aiq sensor name\n");
        return -1;
    }

    LOG_INFO_FMT("Video: aiq sensor=%s iq=%s\n", sensor_name, g_cfg.iq_file_dir);
    rk_aiq_uapi2_sysctl_preInit_devBufCnt(sensor_name, "rkraw_rx", 2);
    if (rk_aiq_uapi2_sysctl_preInit_scene(sensor_name, "normal", "day") != 0) {
        LOG_ERROR_FMT("Video: rk_aiq_uapi2_sysctl_preInit_scene failed\n");
        return -1;
    }

    g_aiq_ctx = rk_aiq_uapi2_sysctl_init(sensor_name, g_cfg.iq_file_dir, NULL, NULL);
    if (!g_aiq_ctx) {
        LOG_ERROR_FMT("Video: rk_aiq_uapi2_sysctl_init failed\n");
        return -1;
    }

    if (rk_aiq_uapi2_sysctl_prepare(g_aiq_ctx, 0, 0, RK_AIQ_WORKING_MODE_NORMAL) != 0) {
        LOG_ERROR_FMT("Video: rk_aiq_uapi2_sysctl_prepare failed\n");
        rk_aiq_uapi2_sysctl_deinit(g_aiq_ctx);
        g_aiq_ctx = NULL;
        return -1;
    }

    if (rk_aiq_uapi2_sysctl_start(g_aiq_ctx) != 0) {
        LOG_ERROR_FMT("Video: rk_aiq_uapi2_sysctl_start failed\n");
        rk_aiq_uapi2_sysctl_deinit(g_aiq_ctx);
        g_aiq_ctx = NULL;
        return -1;
    }

    g_aiq_started = 1;
    return 0;
}

static void video_isp_deinit(void) {
    if (g_aiq_ctx) {
        if (g_aiq_started) {
            rk_aiq_uapi2_sysctl_stop(g_aiq_ctx, false);
        }
        rk_aiq_uapi2_sysctl_deinit(g_aiq_ctx);
        g_aiq_ctx = NULL;
    }
    g_aiq_started = 0;
}

static int video_rtsp_init(void) {
    if (!g_cfg.rtsp_enable) {
        return 0;
    }

    g_rtsp_demo = create_rtsp_demo(g_cfg.rtsp_port);
    if (!g_rtsp_demo) {
        LOG_ERROR_FMT("Video: create_rtsp_demo failed\n");
        return -1;
    }

    g_rtsp_session = rtsp_new_session(g_rtsp_demo, g_cfg.rtsp_path);
    if (!g_rtsp_session) {
        LOG_ERROR_FMT("Video: rtsp_new_session failed\n");
        rtsp_del_demo(g_rtsp_demo);
        g_rtsp_demo = NULL;
        return -1;
    }

    if (strcmp(g_cfg.codec, "H.265") == 0) {
        rtsp_set_video(g_rtsp_session, RTSP_CODEC_ID_VIDEO_H265, NULL, 0);
    } else {
        rtsp_set_video(g_rtsp_session, RTSP_CODEC_ID_VIDEO_H264, NULL, 0);
    }
    rtsp_sync_video_ts(g_rtsp_session, rtsp_get_reltime(), rtsp_get_ntptime());

    LOG_INFO_FMT("Video: RTSP ready: rtsp://<board-ip>:%d%s\n",
                 g_cfg.rtsp_port, g_cfg.rtsp_path);
    return 0;
}

static void video_rtsp_deinit(void) {
    if (g_rtsp_session) {
        rtsp_del_session(g_rtsp_session);
        g_rtsp_session = NULL;
    }

    if (g_rtsp_demo) {
        rtsp_del_demo(g_rtsp_demo);
        g_rtsp_demo = NULL;
    }
}

static void video_rtsp_send(void *data, RK_U32 len, RK_U64 pts) {
    pthread_mutex_lock(&g_rtsp_lock);

    if (g_rtsp_demo && g_rtsp_session && data && len > 0) {
        rtsp_tx_video(g_rtsp_session, (const uint8_t *)data, (int)len, pts);
        rtsp_do_event(g_rtsp_demo);
    }

    pthread_mutex_unlock(&g_rtsp_lock);
}

static int video_vi_dev_init(void) {
    RK_S32 ret;
    VI_DEV_ATTR_S dev_attr;
    VI_DEV_BIND_PIPE_S bind_pipe;

    memset(&dev_attr, 0, sizeof(dev_attr));
    memset(&bind_pipe, 0, sizeof(bind_pipe));

    ret = RK_MPI_VI_GetDevAttr(g_cfg.vi_dev, &dev_attr);
    if (ret == RK_ERR_VI_NOT_CONFIG) {
        ret = RK_MPI_VI_SetDevAttr(g_cfg.vi_dev, &dev_attr);
        if (ret != RK_SUCCESS) {
            LOG_ERROR_FMT("Video: RK_MPI_VI_SetDevAttr failed %#x\n", ret);
            return -1;
        }
    } else if (ret != RK_SUCCESS) {
        LOG_ERROR_FMT("Video: RK_MPI_VI_GetDevAttr failed %#x\n", ret);
        return -1;
    }

    ret = RK_MPI_VI_GetDevIsEnable(g_cfg.vi_dev);
    if (ret != RK_SUCCESS) {
        ret = RK_MPI_VI_EnableDev(g_cfg.vi_dev);
        if (ret != RK_SUCCESS) {
            LOG_ERROR_FMT("Video: RK_MPI_VI_EnableDev failed %#x\n", ret);
            return -1;
        }

        bind_pipe.u32Num = 1;
        bind_pipe.PipeId[0] = g_cfg.vi_pipe;

        ret = RK_MPI_VI_SetDevBindPipe(g_cfg.vi_dev, &bind_pipe);
        if (ret != RK_SUCCESS) {
            LOG_ERROR_FMT("Video: RK_MPI_VI_SetDevBindPipe failed %#x\n", ret);
            return -1;
        }
    }

    return 0;
}

static int video_vi_chn_init(void) {
    RK_S32 ret;
    VI_CHN_ATTR_S attr;

    memset(&attr, 0, sizeof(attr));

    attr.stIspOpt.u32BufCount = g_cfg.vi_buf_count;
    attr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
    attr.stIspOpt.stMaxSize.u32Width = g_cfg.max_width;
    attr.stIspOpt.stMaxSize.u32Height = g_cfg.max_height;

    attr.stSize.u32Width = g_cfg.width;
    attr.stSize.u32Height = g_cfg.height;
    attr.enPixelFormat = RK_FMT_YUV420SP;
    attr.enCompressMode = COMPRESS_MODE_NONE;
    attr.u32Depth = 1;

    ret = RK_MPI_VI_SetChnAttr(g_cfg.vi_pipe, g_cfg.vi_chn, &attr);
    if (ret != RK_SUCCESS) {
        LOG_ERROR_FMT("Video: RK_MPI_VI_SetChnAttr failed %#x\n", ret);
        return -1;
    }

    ret = RK_MPI_VI_EnableChn(g_cfg.vi_pipe, g_cfg.vi_chn);
    if (ret != RK_SUCCESS) {
        LOG_ERROR_FMT("Video: RK_MPI_VI_EnableChn failed %#x\n", ret);
        return -1;
    }

    return 0;
}

static RK_U32 video_h264_profile(void) {
    if (strcmp(g_cfg.h264_profile, "baseline") == 0) {
        return 66;
    }
    if (strcmp(g_cfg.h264_profile, "main") == 0) {
        return 77;
    }
    return 100;
}

static int video_venc_init(void) {
    RK_S32 ret;
    VENC_CHN_ATTR_S attr;
    VENC_RECV_PIC_PARAM_S recv_param;

    memset(&attr, 0, sizeof(attr));

    if (strcmp(g_cfg.codec, "H.265") == 0) {
        attr.stVencAttr.enType = RK_VIDEO_ID_HEVC;
        attr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
        attr.stRcAttr.stH265Cbr.u32Gop = g_cfg.gop;
        attr.stRcAttr.stH265Cbr.u32BitRate = g_cfg.bitrate_kbps;
    } else {
        attr.stVencAttr.enType = RK_VIDEO_ID_AVC;
        attr.stVencAttr.u32Profile = video_h264_profile();
        attr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
        attr.stRcAttr.stH264Cbr.u32Gop = g_cfg.gop;
        attr.stRcAttr.stH264Cbr.u32BitRate = g_cfg.bitrate_kbps;
    }

    attr.stVencAttr.enPixelFormat = RK_FMT_YUV420SP;
    attr.stVencAttr.u32MaxPicWidth = g_cfg.max_width;
    attr.stVencAttr.u32MaxPicHeight = g_cfg.max_height;
    attr.stVencAttr.u32PicWidth = g_cfg.width;
    attr.stVencAttr.u32PicHeight = g_cfg.height;
    attr.stVencAttr.u32VirWidth = align16(g_cfg.width);
    attr.stVencAttr.u32VirHeight = align16(g_cfg.height);
    attr.stVencAttr.u32StreamBufCnt = g_cfg.venc_buf_count;
    attr.stVencAttr.u32BufSize = g_cfg.venc_buf_size;
    attr.stVencAttr.bByFrame = RK_TRUE;
    attr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;

    ret = RK_MPI_VENC_CreateChn(g_cfg.venc_chn, &attr);
    if (ret != RK_SUCCESS) {
        LOG_ERROR_FMT("Video: RK_MPI_VENC_CreateChn failed %#x\n", ret);
        return -1;
    }

    memset(&recv_param, 0, sizeof(recv_param));
    recv_param.s32RecvPicNum = -1;

    ret = RK_MPI_VENC_StartRecvFrame(g_cfg.venc_chn, &recv_param);
    if (ret != RK_SUCCESS) {
        LOG_ERROR_FMT("Video: RK_MPI_VENC_StartRecvFrame failed %#x\n", ret);
        RK_MPI_VENC_DestroyChn(g_cfg.venc_chn);
        return -1;
    }

    return 0;
}

static int video_bind(void) {
    MPP_CHN_S vi;
    MPP_CHN_S venc;

    memset(&vi, 0, sizeof(vi));
    memset(&venc, 0, sizeof(venc));

    vi.enModId = RK_ID_VI;
    vi.s32DevId = g_cfg.vi_dev;
    vi.s32ChnId = g_cfg.vi_chn;

    venc.enModId = RK_ID_VENC;
    venc.s32DevId = 0;
    venc.s32ChnId = g_cfg.venc_chn;

    return RK_MPI_SYS_Bind(&vi, &venc);
}

static void video_unbind(void) {
    MPP_CHN_S vi;
    MPP_CHN_S venc;

    memset(&vi, 0, sizeof(vi));
    memset(&venc, 0, sizeof(venc));

    vi.enModId = RK_ID_VI;
    vi.s32DevId = g_cfg.vi_dev;
    vi.s32ChnId = g_cfg.vi_chn;

    venc.enModId = RK_ID_VENC;
    venc.s32DevId = 0;
    venc.s32ChnId = g_cfg.venc_chn;

    RK_MPI_SYS_UnBind(&vi, &venc);
}

static void video_venc_deinit(void) {
    RK_MPI_VENC_StopRecvFrame(g_cfg.venc_chn);
    RK_MPI_VENC_DestroyChn(g_cfg.venc_chn);
}

static void video_vi_deinit(void) {
    RK_MPI_VI_DisableChn(g_cfg.vi_pipe, g_cfg.vi_chn);
    RK_MPI_VI_DisableDev(g_cfg.vi_dev);
}

static void *video_thread(void *arg) {
    VENC_STREAM_S stream;

    (void)arg;

    memset(&stream, 0, sizeof(stream));
    stream.pstPack = malloc(sizeof(VENC_PACK_S));
    if (!stream.pstPack) {
        LOG_ERROR_FMT("Video: malloc VENC_PACK_S failed\n");
        return NULL;
    }

    while (g_running) {
        RK_S32 ret = RK_MPI_VENC_GetStream(g_cfg.venc_chn, &stream, 1000);
        if (ret == RK_SUCCESS) {
            void *data = RK_MPI_MB_Handle2VirAddr(stream.pstPack->pMbBlk);

            video_rtsp_send(data, stream.pstPack->u32Len, stream.pstPack->u64PTS);

            ret = RK_MPI_VENC_ReleaseStream(g_cfg.venc_chn, &stream);
            if (ret != RK_SUCCESS) {
                LOG_ERROR_FMT("Video: RK_MPI_VENC_ReleaseStream failed %#x\n", ret);
            }
        } else {
            LOG_WARN_FMT("Video: RK_MPI_VENC_GetStream timeout %#x\n", ret);
        }
    }

    free(stream.pstPack);
    return NULL;
}

static int video_start_pipeline(void) {
    RK_S32 ret;

    if (!g_cfg.enable) {
        LOG_WARN_FMT("Video: disabled by config\n");
        return 0;
    }

    if (video_isp_init() != 0) {
        return -1;
    }

    ret = RK_MPI_SYS_Init();
    if (ret != RK_SUCCESS) {
        LOG_ERROR_FMT("Video: RK_MPI_SYS_Init failed %#x\n", ret);
        video_isp_deinit();
        return -1;
    }

    if (video_rtsp_init() != 0)
        goto err_sys;

    if (video_vi_dev_init() != 0)
        goto err_rtsp;

    if (video_vi_chn_init() != 0)
        goto err_vi_dev;

    if (video_venc_init() != 0)
        goto err_vi_chn;

    if (video_bind() != RK_SUCCESS)
        goto err_venc;

    g_running = 1;
    if (pthread_create(&g_thread, NULL, video_thread, NULL) != 0) {
        g_running = 0;
        goto err_bind;
    }

    g_started = 1;
    return 0;

err_bind:
    video_unbind();
err_venc:
    video_venc_deinit();
err_vi_chn:
    RK_MPI_VI_DisableChn(g_cfg.vi_pipe, g_cfg.vi_chn);
err_vi_dev:
    RK_MPI_VI_DisableDev(g_cfg.vi_dev);
err_rtsp:
    video_rtsp_deinit();
err_sys:
    RK_MPI_SYS_Exit();
    video_isp_deinit();
    return -1;
}

static void video_stop_pipeline(void) {
    if (!g_started)
        return;

    g_running = 0;
    pthread_join(g_thread, NULL);

    video_unbind();
    video_venc_deinit();
    video_vi_deinit();
    video_rtsp_deinit();
    RK_MPI_SYS_Exit();
    video_isp_deinit();

    g_started = 0;
}

static int video_module_init(void) {
    LOG_INFO_FMT("Video: init\n");
    video_config_load(&g_cfg);
    return 0;
}

static int video_module_start(void) {
    LOG_INFO_FMT("Video: start\n");
    return video_start_pipeline();
}

static int video_module_stop(void) {
    LOG_INFO_FMT("Video: stop\n");
    video_stop_pipeline();

    if (g_param_ready) {
        rk_param_deinit();
        g_param_ready = 0;
    }

    return 0;
}

static int video_module_health_check(void) {
    if (!g_cfg.enable) {
        return 0;
    }
    return g_running ? 0 : -1;
}

static module_t video_module = {
    .name = "Video",
    .version = "1.0.0",
    .author = "Team",
    .init = video_module_init,
    .start = video_module_start,
    .stop = video_module_stop,
    .health_check = video_module_health_check,
};

__attribute__((constructor))
void video_module_register(void) {
    module_register(&video_module);
}


