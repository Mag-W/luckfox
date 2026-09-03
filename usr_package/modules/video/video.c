#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "module.h"
#include "log.h"

/* 可选配置读取（如果你不想依赖 param，可注释掉并使用默认值） */
#include "param.h"

/* RK MPI */
#include "rk_comm_sys.h"
#include "rk_comm_vi.h"
#include "rk_comm_venc.h"
#include "rk_mpi_sys.h"
#include "rk_mpi_vi.h"
#include "rk_mpi_venc.h"
#include "rk_mpi_mb.h"

/* RTSP */
#include "rtsp_demo.h"

/* ===================== 可选 RKAIQ（默认不启） ===================== */
#ifdef ENABLE_RKAIQ
#include "rkaiq/uAPI2/rk_aiq_user_api2_sysctl.h"
static rk_aiq_sys_ctx_t *g_aiq_ctx = NULL;
static int g_aiq_started = 0;
#else
static void *g_aiq_ctx = NULL;
static int g_aiq_started = 0;
#endif

/* ===================== 配置 ===================== */
typedef struct {
    int enable;

    int enable_aiq;                 /* 仅在 ENABLE_RKAIQ 编译开关打开时生效 */
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
    char codec[16];                 /* "H.264" / "H.265" */
    char h264_profile[16];          /* baseline/main/high */
    int sensor_fps;                 /* sensor 实际输出帧率，sc3336@2304x1296=25 */
    int fps;                        /* 目标编码/出流帧率 */
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
static pthread_t g_rtsp_event_thread;
static int g_rtsp_event_running = 0;

static rtsp_demo_handle g_rtsp_demo = NULL;
static rtsp_session_handle g_rtsp_session = NULL;
static pthread_mutex_t g_rtsp_lock = PTHREAD_MUTEX_INITIALIZER;

static RK_U32 align16(RK_U32 v) { return (v + 15) & ~15; }

static void str_copy(char *dst, size_t n, const char *src) {
    if (!dst || n == 0) return;
    snprintf(dst, n, "%s", src ? src : "");
}

static void video_config_set_default(video_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->enable = 1;

    cfg->enable_aiq = 1;
    str_copy(cfg->iq_file_dir, sizeof(cfg->iq_file_dir), "/oem/usr/share/iqfiles");

    cfg->vi_dev = 0;
    cfg->vi_pipe = 0;
    cfg->vi_chn = 0;
    cfg->vi_buf_count = 4;

    cfg->venc_chn = 0;
    cfg->width = 2304;
    cfg->height = 1296;
    cfg->max_width = 2304;
    cfg->max_height = 1296;
    str_copy(cfg->codec, sizeof(cfg->codec), "H.265");
    str_copy(cfg->h264_profile, sizeof(cfg->h264_profile), "baseline");
    /* sc3336 2304x1296 驱动表为 25fps，不可写死 30，否则 VI/VENC 双重错控会打坏 IDR */
    cfg->sensor_fps = 25;
    cfg->fps = 25;
    cfg->gop = 50;
    cfg->bitrate_kbps = 3072;
    cfg->venc_buf_count = 4;
    /* 与 sample 一致：过小会截断 IDR，表现为隔几秒灰屏 */
    cfg->venc_buf_size = cfg->width * cfg->height * 3 / 2;

    cfg->rtsp_enable = 1;
    cfg->rtsp_port = 8554;
    str_copy(cfg->rtsp_path, sizeof(cfg->rtsp_path), "/live/0");
}

static void video_config_load(video_config_t *cfg) {
    video_config_set_default(cfg);

    /* 配置读取优先级：
     *   1. /etc/video.ini        （用户自定义，见 usr_package/config/video.ini）
     *   2. /userdata/video.ini  （/etc 只读时的备用，放可写的 /userdata）
     *   3. /userdata/rkipc.ini  （rockchip 标准配置）
     *   都失败时使用内置默认值（已保证 AIQ 开启，可出流）
     */
    if (rk_param_init("/etc/video.ini") != 0 &&
        rk_param_init("/userdata/video.ini") != 0 &&
        rk_param_init("/userdata/rkipc.ini") != 0) {
        LOG_WARN_FMT("video: no ini found, use built-in defaults\n");
        LOG_INFO_FMT("video cfg: %dx%d codec=%s rtsp=%d path=%s\n",
                     cfg->width, cfg->height, cfg->codec, cfg->rtsp_port, cfg->rtsp_path);
        return;
    }
    g_param_ready = 1;

    cfg->enable = rk_param_get_int("video.0:enable", cfg->enable);

    cfg->enable_aiq = rk_param_get_int("video.source:enable_aiq", cfg->enable_aiq);
    str_copy(cfg->iq_file_dir, sizeof(cfg->iq_file_dir),
             rk_param_get_string("video.source:iq_file_dir", cfg->iq_file_dir));

    cfg->vi_dev = rk_param_get_int("video.source:vi_dev", cfg->vi_dev);
    cfg->vi_pipe = rk_param_get_int("video.source:vi_pipe", cfg->vi_pipe);
    cfg->vi_chn = rk_param_get_int("video.source:vi_chn", cfg->vi_chn);
    cfg->vi_buf_count = rk_param_get_int("video.source:buf_count", cfg->vi_buf_count);

    cfg->venc_chn = rk_param_get_int("video.0:venc_chn", cfg->venc_chn);
    cfg->width = rk_param_get_int("video.0:width", cfg->width);
    cfg->height = rk_param_get_int("video.0:height", cfg->height);
    cfg->max_width = rk_param_get_int("video.0:max_width", cfg->max_width);
    cfg->max_height = rk_param_get_int("video.0:max_height", cfg->max_height);
    str_copy(cfg->codec, sizeof(cfg->codec),
             rk_param_get_string("video.0:output_data_type", cfg->codec));
    str_copy(cfg->h264_profile, sizeof(cfg->h264_profile),
             rk_param_get_string("video.0:h264_profile", cfg->h264_profile));

    /* sensor 帧率：优先 video.source:sensor_fps，其次官方 isp.0.adjustment:fps */
    cfg->sensor_fps = rk_param_get_int("video.source:sensor_fps", cfg->sensor_fps);
    cfg->sensor_fps = rk_param_get_int("isp.0.adjustment:fps", cfg->sensor_fps);
    if (cfg->sensor_fps <= 0) cfg->sensor_fps = 25;

    /* 帧率：用户自定义 ini 用 fps 键；标准 rkipc.ini 用 dst_frame_rate_num */
    {
        const char *s = rk_param_get_string("video.0:fps", NULL);
        if (s && s[0]) {
            cfg->fps = atoi(s);
        } else {
            cfg->fps = rk_param_get_int("video.0:dst_frame_rate_num", cfg->fps);
        }
    }
    if (cfg->fps <= 0) cfg->fps = cfg->sensor_fps;
    if (cfg->fps > cfg->sensor_fps) cfg->fps = cfg->sensor_fps;

    cfg->gop = rk_param_get_int("video.0:gop", cfg->gop);
    cfg->bitrate_kbps = rk_param_get_int("video.0:max_rate", cfg->bitrate_kbps);
    cfg->venc_buf_count = rk_param_get_int("video.0:buffer_count", cfg->venc_buf_count);
    cfg->venc_buf_size = rk_param_get_int("video.0:buffer_size", cfg->venc_buf_size);

    /* RTSP：用户自定义 ini 用 [rtsp] enable；旧的 video.source:enable_rtsp 也兼容 */
    cfg->rtsp_enable = rk_param_get_int("rtsp:enable", cfg->rtsp_enable);
    cfg->rtsp_enable = rk_param_get_int("video.source:enable_rtsp", cfg->rtsp_enable);
    cfg->rtsp_port = rk_param_get_int("rtsp:port", cfg->rtsp_port);
    str_copy(cfg->rtsp_path, sizeof(cfg->rtsp_path),
             rk_param_get_string("rtsp:path", cfg->rtsp_path));

    LOG_INFO_FMT("video cfg: %dx%d codec=%s aiq=%d iq=%s sensor_fps=%d fps=%d gop=%d rtsp=%d port=%d path=%s\n",
                 cfg->width, cfg->height, cfg->codec, cfg->enable_aiq,
                 cfg->iq_file_dir, cfg->sensor_fps, cfg->fps, cfg->gop,
                 cfg->rtsp_enable, cfg->rtsp_port, cfg->rtsp_path);
}

/* ===================== ISP/AIQ（可选） ===================== */
#ifdef ENABLE_RKAIQ
static int video_isp_init(void) {
    if (!g_cfg.enable_aiq) return 0;

    rk_aiq_static_info_t st;
    memset(&st, 0, sizeof(st));
    setenv("HDR_MODE", "0", 1);

    if (rk_aiq_uapi2_sysctl_enumStaticMetasByPhyId(0, &st) != 0) {
        LOG_ERROR_FMT("rkaiq enum meta failed\n");
        return -1;
    }
    const char *sensor_name = st.sensor_info.sensor_name;
    if (!sensor_name || !sensor_name[0]) return -1;

    rk_aiq_uapi2_sysctl_preInit_devBufCnt(sensor_name, "rkraw_rx", 2);
    rk_aiq_uapi2_sysctl_preInit_scene(sensor_name, "normal", "day");

    g_aiq_ctx = rk_aiq_uapi2_sysctl_init(sensor_name, g_cfg.iq_file_dir, NULL, NULL);
    if (!g_aiq_ctx) return -1;

    if (rk_aiq_uapi2_sysctl_prepare(g_aiq_ctx, 0, 0, RK_AIQ_WORKING_MODE_NORMAL) != 0) {
        rk_aiq_uapi2_sysctl_deinit(g_aiq_ctx);
        g_aiq_ctx = NULL;
        return -1;
    }
    if (rk_aiq_uapi2_sysctl_start(g_aiq_ctx) != 0) {
        rk_aiq_uapi2_sysctl_deinit(g_aiq_ctx);
        g_aiq_ctx = NULL;
        return -1;
    }

    g_aiq_started = 1;
    return 0;
}
static void video_isp_deinit(void) {
    if (g_aiq_ctx) {
        if (g_aiq_started) rk_aiq_uapi2_sysctl_stop(g_aiq_ctx, false);
        rk_aiq_uapi2_sysctl_deinit(g_aiq_ctx);
        g_aiq_ctx = NULL;
        g_aiq_started = 0;
    }
}
#else
static int video_isp_init(void) {
    g_aiq_ctx = NULL;
    g_aiq_started = 0;
    return 0;
}
static void video_isp_deinit(void) { }
#endif

/* ===================== RTSP ===================== */
/*
 * TCP 断连后若只在 rtsp_tx_video 之后才 do_event，发送可能堵在已断开的 socket 上，
 * 导致无法 accept 新客户端，只能重启。处理：
 * 1) 独立线程周期性 do_event（取流线程在 GetStream 等待时不占锁，可清会话）
 * 2) 发送前先 do_event，避免继续往已断开连接写
 * 3) 忽略 SIGPIPE，避免写已关闭 TCP 时进程被杀
 */
static void *video_rtsp_event_thread(void *arg) {
    (void)arg;
    while (g_rtsp_event_running) {
        pthread_mutex_lock(&g_rtsp_lock);
        if (g_rtsp_demo)
            rtsp_do_event(g_rtsp_demo);
        pthread_mutex_unlock(&g_rtsp_lock);
        usleep(20 * 1000);
    }
    return NULL;
}

static int video_rtsp_init(void) {
    if (!g_cfg.rtsp_enable) return 0;

    /* 写已关闭的 TCP 时不要被 SIGPIPE 打死，让 send 返回错误以便库回收会话 */
    signal(SIGPIPE, SIG_IGN);

    g_rtsp_demo = create_rtsp_demo(g_cfg.rtsp_port);
    if (!g_rtsp_demo) return -1;

    g_rtsp_session = rtsp_new_session(g_rtsp_demo, g_cfg.rtsp_path);
    if (!g_rtsp_session) {
        rtsp_del_demo(g_rtsp_demo);
        g_rtsp_demo = NULL;
        return -1;
    }

    if (!strcmp(g_cfg.codec, "H.265"))
        rtsp_set_video(g_rtsp_session, RTSP_CODEC_ID_VIDEO_H265, NULL, 0);
    else
        rtsp_set_video(g_rtsp_session, RTSP_CODEC_ID_VIDEO_H264, NULL, 0);

    rtsp_sync_video_ts(g_rtsp_session, rtsp_get_reltime(), rtsp_get_ntptime());

    g_rtsp_event_running = 1;
    if (pthread_create(&g_rtsp_event_thread, NULL, video_rtsp_event_thread, NULL) != 0) {
        g_rtsp_event_running = 0;
        rtsp_del_session(g_rtsp_session);
        g_rtsp_session = NULL;
        rtsp_del_demo(g_rtsp_demo);
        g_rtsp_demo = NULL;
        return -1;
    }

    LOG_INFO_FMT("RTSP ready: rtsp://<board-ip>:%d%s\n", g_cfg.rtsp_port, g_cfg.rtsp_path);
    return 0;
}

static void video_rtsp_deinit(void) {
    if (g_rtsp_event_running) {
        g_rtsp_event_running = 0;
        pthread_join(g_rtsp_event_thread, NULL);
    }

    pthread_mutex_lock(&g_rtsp_lock);
    if (g_rtsp_session) {
        rtsp_del_session(g_rtsp_session);
        g_rtsp_session = NULL;
    }
    if (g_rtsp_demo) {
        rtsp_del_demo(g_rtsp_demo);
        g_rtsp_demo = NULL;
    }
    pthread_mutex_unlock(&g_rtsp_lock);
}

static void video_rtsp_send(void *data, RK_U32 len, RK_U64 pts) {
    pthread_mutex_lock(&g_rtsp_lock);
    if (g_rtsp_demo) {
        /* 先处理 TEARDOWN/断连/新连接，再发送，避免往死连接写导致卡住 */
        rtsp_do_event(g_rtsp_demo);
        if (g_rtsp_session && data && len > 0)
            rtsp_tx_video(g_rtsp_session, (const uint8_t *)data, (int)len, pts);
    }
    pthread_mutex_unlock(&g_rtsp_lock);
}

/* ===================== VI / VENC ===================== */
static int video_vi_init(void) {
    RK_S32 ret;
    VI_DEV_ATTR_S dev_attr;
    VI_DEV_BIND_PIPE_S bind_pipe;
    VI_CHN_ATTR_S chn_attr;

    memset(&dev_attr, 0, sizeof(dev_attr));
    memset(&bind_pipe, 0, sizeof(bind_pipe));
    memset(&chn_attr, 0, sizeof(chn_attr));

    ret = RK_MPI_VI_GetDevAttr(g_cfg.vi_dev, &dev_attr);
    if (ret == RK_ERR_VI_NOT_CONFIG) {
        ret = RK_MPI_VI_SetDevAttr(g_cfg.vi_dev, &dev_attr);
        if (ret != RK_SUCCESS) return -1;
    } else if (ret != RK_SUCCESS) {
        return -1;
    }

    ret = RK_MPI_VI_GetDevIsEnable(g_cfg.vi_dev);
    if (ret != RK_SUCCESS) {
        ret = RK_MPI_VI_EnableDev(g_cfg.vi_dev);
        if (ret != RK_SUCCESS) return -1;

        bind_pipe.u32Num = 1;
        bind_pipe.PipeId[0] = g_cfg.vi_pipe;
        ret = RK_MPI_VI_SetDevBindPipe(g_cfg.vi_dev, &bind_pipe);
        if (ret != RK_SUCCESS) return -1;
    }

    chn_attr.stIspOpt.u32BufCount = g_cfg.vi_buf_count;
    chn_attr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
    /* stMaxSize 应为 sensor 原始最大尺寸(sc3336 = 2304x1296)，ISP 缩放后输出 stSize */
    chn_attr.stIspOpt.stMaxSize.u32Width = 2304;
    chn_attr.stIspOpt.stMaxSize.u32Height = 1296;
    chn_attr.stSize.u32Width = g_cfg.width;
    chn_attr.stSize.u32Height = g_cfg.height;
    chn_attr.enPixelFormat = RK_FMT_YUV420SP;
    chn_attr.enCompressMode = COMPRESS_MODE_NONE;
    chn_attr.u32Depth = 1;
    /* 与官方 rk_video_set_frame_rate(den==1) 一致：
     * VI 按 sensor_fps → fps 丢帧；VENC 的 src/dst 都设为 fps（见 video_venc_init） */
    chn_attr.stFrameRate.s32SrcFrameRate = g_cfg.sensor_fps;
    chn_attr.stFrameRate.s32DstFrameRate = g_cfg.fps;

    ret = RK_MPI_VI_SetChnAttr(g_cfg.vi_pipe, g_cfg.vi_chn, &chn_attr);
    if (ret != RK_SUCCESS) return -1;

    ret = RK_MPI_VI_EnableChn(g_cfg.vi_pipe, g_cfg.vi_chn);
    if (ret != RK_SUCCESS) return -1;

    return 0;
}

static void video_vi_deinit(void) {
    RK_MPI_VI_DisableChn(g_cfg.vi_pipe, g_cfg.vi_chn);
    RK_MPI_VI_DisableDev(g_cfg.vi_dev);
}

static RK_U32 video_h264_profile(void) {
    if (!strcmp(g_cfg.h264_profile, "baseline")) return 66;
    if (!strcmp(g_cfg.h264_profile, "main")) return 77;
    return 100;
}

static int video_venc_init(void) {
    RK_S32 ret;
    VENC_CHN_ATTR_S attr;
    VENC_RECV_PIC_PARAM_S recv;

    memset(&attr, 0, sizeof(attr));
    memset(&recv, 0, sizeof(recv));

    if (!strcmp(g_cfg.codec, "H.265")) {
        attr.stVencAttr.enType = RK_VIDEO_ID_HEVC;
        attr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
        attr.stRcAttr.stH265Cbr.u32BitRate = g_cfg.bitrate_kbps;
        attr.stRcAttr.stH265Cbr.u32Gop = g_cfg.gop;
        /* VI 已按 sensor→fps 控帧，VENC 输入已是 fps，src/dst 都必须等于 fps。
         * 此前写死 src=30 会与真实 25fps 冲突，易导致周期性 IDR 异常/灰屏。 */
        attr.stRcAttr.stH265Cbr.u32SrcFrameRateNum = g_cfg.fps;
        attr.stRcAttr.stH265Cbr.u32SrcFrameRateDen = 1;
        attr.stRcAttr.stH265Cbr.fr32DstFrameRateNum = g_cfg.fps;
        attr.stRcAttr.stH265Cbr.fr32DstFrameRateDen = 1;
    } else {
        attr.stVencAttr.enType = RK_VIDEO_ID_AVC;
        attr.stVencAttr.u32Profile = video_h264_profile();
        attr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
        attr.stRcAttr.stH264Cbr.u32BitRate = g_cfg.bitrate_kbps;
        attr.stRcAttr.stH264Cbr.u32Gop = g_cfg.gop;
        attr.stRcAttr.stH264Cbr.u32SrcFrameRateNum = g_cfg.fps;
        attr.stRcAttr.stH264Cbr.u32SrcFrameRateDen = 1;
        attr.stRcAttr.stH264Cbr.fr32DstFrameRateNum = g_cfg.fps;
        attr.stRcAttr.stH264Cbr.fr32DstFrameRateDen = 1;
    }

    attr.stVencAttr.enPixelFormat = RK_FMT_YUV420SP;
    /* VENC 最大编码尺寸对齐实际编码尺寸，避免 H265 按过大 max 分配参考帧缓冲导致异常 */
    attr.stVencAttr.u32MaxPicWidth = g_cfg.width;
    attr.stVencAttr.u32MaxPicHeight = g_cfg.height;
    attr.stVencAttr.u32PicWidth = g_cfg.width;
    attr.stVencAttr.u32PicHeight = g_cfg.height;
    /* 虚拟尺寸必须与 VI 输出的 buffer 尺寸严格一致，否则 rockit 报
     * "frame info no equal set drop" 并丢帧。RV1106 上 VI 输出 YUV420SP
     * buffer 的虚拟宽高即实际宽高（不额外 16 对齐高度） */
    attr.stVencAttr.u32VirWidth = g_cfg.width;
    attr.stVencAttr.u32VirHeight = g_cfg.height;
    attr.stVencAttr.u32StreamBufCnt = g_cfg.venc_buf_count;
    attr.stVencAttr.u32BufSize = g_cfg.venc_buf_size > 0 ? g_cfg.venc_buf_size
                                                         : (g_cfg.width * g_cfg.height * 3 / 2);
    attr.stVencAttr.bByFrame = RK_TRUE;
    attr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;

    ret = RK_MPI_VENC_CreateChn(g_cfg.venc_chn, &attr);
    if (ret != RK_SUCCESS) return -1;

    recv.s32RecvPicNum = -1;
    ret = RK_MPI_VENC_StartRecvFrame(g_cfg.venc_chn, &recv);
    if (ret != RK_SUCCESS) {
        RK_MPI_VENC_DestroyChn(g_cfg.venc_chn);
        return -1;
    }

    /* 帧率已在 VI + VENC RC 中对齐，不再额外 SetChnParam 控帧，避免双重丢帧 */

    return 0;
}

static void video_venc_deinit(void) {
    RK_MPI_VENC_StopRecvFrame(g_cfg.venc_chn);
    RK_MPI_VENC_DestroyChn(g_cfg.venc_chn);
}

static int video_bind(void) {
    MPP_CHN_S vi, venc;
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
    MPP_CHN_S vi, venc;
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

/* ===================== 编码取流线程 ===================== */
/* bByFrame 下多数一帧一 pack；若出现多 pack（VPS/SPS/PPS/IDR 分开），合并成一次发送，
 * 避免播放器把参数集和 IDR 当成不同 AU。发送前 FlushCache，保证 CPU 读到硬件写完的码流。 */
static void *video_stream_thread(void *arg) {
    (void)arg;
    VENC_STREAM_S st;
    memset(&st, 0, sizeof(st));
    st.pstPack = malloc(sizeof(VENC_PACK_S) * 8);
    if (!st.pstPack) return NULL;

    while (g_running) {
        RK_S32 ret = RK_MPI_VENC_GetStream(g_cfg.venc_chn, &st, 1000);
        if (ret != RK_SUCCESS)
            continue;

        RK_U32 npack = st.u32PackCount > 0 ? st.u32PackCount : 1;
        if (npack > 8)
            npack = 8;

        for (RK_U32 p = 0; p < npack; p++) {
            if (st.pstPack[p].pMbBlk)
                RK_MPI_SYS_MmzFlushCache(st.pstPack[p].pMbBlk, RK_TRUE);
        }

        if (npack == 1) {
            void *data = RK_MPI_MB_Handle2VirAddr(st.pstPack[0].pMbBlk);
            RK_U32 plen = st.pstPack[0].u32Len;
            if (data && plen > 0)
                video_rtsp_send(data, plen, st.pstPack[0].u64PTS);
        } else {
            RK_U32 total = 0;
            for (RK_U32 p = 0; p < npack; p++)
                total += st.pstPack[p].u32Len;
            if (total > 0) {
                uint8_t *buf = (uint8_t *)malloc(total);
                if (buf) {
                    RK_U32 off = 0;
                    for (RK_U32 p = 0; p < npack; p++) {
                        void *data = RK_MPI_MB_Handle2VirAddr(st.pstPack[p].pMbBlk);
                        RK_U32 plen = st.pstPack[p].u32Len;
                        if (!data || plen == 0)
                            continue;
                        memcpy(buf + off, data, plen);
                        off += plen;
                    }
                    if (off > 0)
                        video_rtsp_send(buf, off, st.pstPack[0].u64PTS);
                    free(buf);
                }
            }
        }

        RK_MPI_VENC_ReleaseStream(g_cfg.venc_chn, &st);
    }

    free(st.pstPack);
    return NULL;
}

/* ===================== 生命周期 ===================== */
static int video_start_pipeline(void) {
    if (!g_cfg.enable) {
        LOG_WARN_FMT("video disabled by config\n");
        return 0;
    }

    if (video_isp_init() != 0) return -1;
    if (RK_MPI_SYS_Init() != RK_SUCCESS) {
        video_isp_deinit();
        return -1;
    }

    if (video_rtsp_init() != 0) goto err_sys;
    if (video_vi_init() != 0) goto err_rtsp;
    if (video_venc_init() != 0) goto err_vi;
    if (video_bind() != RK_SUCCESS) goto err_venc;

    g_running = 1;
    if (pthread_create(&g_thread, NULL, video_stream_thread, NULL) != 0) {
        g_running = 0;
        goto err_bind;
    }

    g_started = 1;
    return 0;

err_bind:
    video_unbind();
err_venc:
    video_venc_deinit();
err_vi:
    video_vi_deinit();
err_rtsp:
    video_rtsp_deinit();
err_sys:
    RK_MPI_SYS_Exit();
    video_isp_deinit();
    return -1;
}

static void video_stop_pipeline(void) {
    if (!g_started) return;

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

/* module callbacks */
static int video_module_init(void) {
    LOG_INFO_FMT("video module init\n");
    video_config_load(&g_cfg);
    return 0;
}
static int video_module_start(void) {
    LOG_INFO_FMT("video module start\n");
    return video_start_pipeline();
}
static int video_module_stop(void) {
    LOG_INFO_FMT("video module stop\n");
    video_stop_pipeline();
    if (g_param_ready) {
        rk_param_deinit();
        g_param_ready = 0;
    }
    return 0;
}
static int video_module_health_check(void) {
    if (!g_cfg.enable) return 0;
    return g_running ? 0 : -1;
}

static module_t video_module = {
    .name = "video",
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