/*
 * Copyright 2015 Rockchip Electronics Co. LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>

#include "mpp_mem.h"
#include "mpp_debug.h"
#include "mpp_common.h"

#include "rga.h"
#include "rga_api.h"
#include "../rga3/NormalRga.h"

static RK_U32 rga_debug = 0;

#define RGB_DBG_FUNCTION    (0x00000001)
#define RGB_DBG_COPY        (0x00000002)
#define RGB_DBG_DUP_FIELD   (0x00000004)

#define rga_dbg(flag, fmt, ...) _mpp_dbg(rga_debug, flag, fmt, ## __VA_ARGS__)
#define rga_dbg_func(fmt, ...)  _mpp_dbg_f(rga_debug, RGB_DBG_FUNCTION, fmt, ## __VA_ARGS__)
#define rga_dbg_copy(fmt, ...)  _mpp_dbg(rga_debug, RGB_DBG_COPY, fmt, ## __VA_ARGS__)
#define rga_dbg_dup(fmt, ...)   _mpp_dbg(rga_debug, RGB_DBG_DUP_FIELD, fmt, ## __VA_ARGS__)
static void RgaLogOutRgaReqV2(RgaReq rgaReg);

#define DEFAULT_RGA_DEV     "/dev/rga"

typedef struct RgaCtxImpl_t {
    RK_S32 rga_fd;

    // context holds only one request structure and serial process all input
    RgaReq request;

    void *ctx; // NormalRga
} RgaCtxImpl;

static int is_yuv_format(int fmt)
{
    if (fmt >= RGA_FMT_YCbCr_422_SP && fmt <= RGA_FMT_YCrCb_420_P) {
        return 1;
    }

    return 0;
}

static int is_rgb_format(int fmt)
{
    if (fmt >= RGA_FMT_RGBA_8888 && fmt <= RGA_FMT_BGR_888) {
        return 1;
    }

    return 0;
}

static RgaFormat rga_fmt_map(MppFrameFormat fmt)
{
    RgaFormat ret;

    switch (fmt) {
    case MPP_FMT_YUV420P:
        ret = RGA_FMT_YCbCr_420_P;
        break;
    case MPP_FMT_YUV420SP:
        ret = RGA_FMT_YCbCr_420_SP;
        break;
    case MPP_FMT_YUV422P:
        ret = RGA_FMT_YCbCr_422_P;
        break;
    case MPP_FMT_YUV422SP:
        ret = RGA_FMT_YCrCb_422_SP;
        break;
    case MPP_FMT_RGB565:
        ret = RGA_FMT_RGB_565;
        break;
    case MPP_FMT_RGB888:
        ret = RGA_FMT_RGB_888;
        break;
    case MPP_FMT_ARGB8888:
        ret = RGA_FMT_RGBA_8888;
        break;
    default:
        ret = RGA_FMT_BUTT;
        mpp_err("unsupport mpp fmt %d found\n", fmt);
        break;
    }

    return ret;
}

MPP_RET rga_init(RgaCtx *ctx)
{
    MPP_RET ret = MPP_OK;
    RgaCtxImpl *impl = NULL;

    rga_dbg_func("in\n");

    *ctx = NULL;

    impl = mpp_malloc(RgaCtxImpl, 1);
    if (!impl) {
        mpp_err_f("malloc context failed\n");
        ret = MPP_ERR_NULL_PTR;
        goto END;
    }

    impl->rga_fd = open(DEFAULT_RGA_DEV, O_RDWR | O_CLOEXEC, 0);
    if (impl->rga_fd < 0) {
        mpp_err_f("open device failed\n");
        mpp_free(impl);
        impl = NULL;
        ret = MPP_ERR_OPEN_FILE;
        goto END;
    }

    RgaInit(&impl->ctx);

END:
    *ctx = impl;
    rga_dbg_func("out\n");
    return ret;
}

MPP_RET rga_deinit(RgaCtx ctx)
{
    MPP_RET ret = MPP_OK;
    RgaCtxImpl *impl = NULL;

    rga_dbg_func("in\n");

    impl = (RgaCtxImpl *)ctx;
    if (!impl) {
        mpp_err_f("invalid input");
        ret = MPP_ERR_NULL_PTR;
        goto END;
    }
    
    RgaDeInit(&impl->ctx);

    if (impl->rga_fd >= 0) {
        close(impl->rga_fd);
        impl->rga_fd = -1;
    }

    mpp_free(impl);
END:
    rga_dbg_func("out\n");
    return ret;
}

static MPP_RET rga_ioctl(RgaCtxImpl *impl)
{
    RgaLogOutRgaReqV2(impl->request);

    int io_ret = ioctl(impl->rga_fd, RGA_BLIT_SYNC, &impl->request);
    if (io_ret) {
        mpp_err("rga ioctl failed errno:%d %s", errno, strerror(errno));
        return MPP_NOK;
    }

    return MPP_OK;
}

static MPP_RET config_rga_image(RgaImg *img, MppFrame frame)
{
    RgaFormat fmt = rga_fmt_map(mpp_frame_get_fmt(frame));
    MppBuffer buf = mpp_frame_get_buffer(frame);
    RK_U32 width  = mpp_frame_get_width(frame);
    RK_U32 height = mpp_frame_get_height(frame);
    RK_U32 h_str  = mpp_frame_get_hor_stride(frame);
    RK_U32 v_str  = mpp_frame_get_ver_stride(frame);
    RK_S32 fd = mpp_buffer_get_fd(buf);

    if (fmt >= RGA_FMT_BUTT) {
        mpp_err("invalid input format for rga process %d\n", fmt);
        return MPP_NOK;
    }

    memset(img, 0, sizeof(RgaImg));
    img->yrgb_addr = fd;
    img->format = (RK_U32)fmt;
    img->act_w = width;
    img->act_h = height;
    img->vir_w = h_str;
    img->vir_h = v_str;

    return MPP_OK;
}

static MPP_RET config_rga_yuv2rgb_mode(RgaCtx ctx)
{
    RgaCtxImpl *impl = (RgaCtxImpl *)ctx;
    RgaReq *request = &impl->request;

    /*
     * yuv2rgb_mode only set when translate yuv to rgb, or rga to yuv.
     * If format of input and output are both yuv or rga, set yuv2rgb_mode to 0.
     */
    int src_format = request->src.format;
    int dst_format = request->dst.format;

    request->yuv2rgb_mode = 0;
    if (is_yuv_format(src_format) && is_rgb_format(dst_format)) {
        /* Special config for yuv to rgb */
        request->yuv2rgb_mode |= 0x1 << 0;
    } else if (is_rgb_format(src_format) && is_yuv_format(dst_format)) {
        /* Special config for rgb to yuv */
        request->yuv2rgb_mode = (2 << 4);
    }

    return MPP_OK;
}

static MPP_RET rga_control_v2(RgaCtx ctx, RgaCmd cmd, void *param)
{
    if (NULL == ctx) {
        mpp_err_f("invalid NULL input\n");
        return MPP_ERR_NULL_PTR;
    }

    rga_dbg_func("in\n");

    MPP_RET ret = MPP_OK;
    RgaCtxImpl *impl = (RgaCtxImpl *)ctx;
    RgaReq *request = &impl->request;

    switch (cmd) {
    case RGA_CMD_INIT : {
        memset(request, 0, sizeof(*request));
        request->mmu_info.mmu_en = 1;
        request->mmu_info.mmu_flag = 1;
        request->mmu_info.mmu_flag = ((2 & 0x3) << 4) | 1;
        request->mmu_info.mmu_flag |= (1 << 31) | (1 << 10) | (1 << 8);
    } break;
    case RGA_CMD_SET_SRC : {
        if (NULL == param) {
            mpp_err("invalid NULL param for setup source\n");
            ret = MPP_NOK;
            break;
        }

        MppFrame *src = (MppFrame *)param;
        ret = config_rga_image(&request->src, src);
    } break;
    case RGA_CMD_SET_DST : {
        if (NULL == param) {
            mpp_err("invalid NULL param for setup destination\n");
            ret = MPP_NOK;
            break;
        }

        MppFrame *dst = (MppFrame *)param;
        ret = config_rga_image(&request->dst, dst);
        // When config dst setup default clip
        RK_U32 width  = mpp_frame_get_width(dst);
        RK_U32 height = mpp_frame_get_height(dst);
        request->clip.xmin = 0;
        request->clip.xmax = width - 1;
        request->clip.ymin = 0;
        request->clip.ymax = height - 1;
    } break;
    case RGA_CMD_RUN_SYNC : {
        config_rga_yuv2rgb_mode(ctx);
        ret = rga_ioctl(impl);
    } break;
    default : {
        mpp_err("invalid command %d\n", cmd);
        ret = MPP_NOK;
    } break;
    }

    rga_dbg_func("out\n");
    return ret;
}

// sample for copy function
static MPP_RET rga_copy_v2(RgaCtx ctx, MppFrame src, MppFrame dst)
{
    MPP_RET ret = MPP_OK;
    RgaCtxImpl *impl = (RgaCtxImpl *)ctx;
    MppBuffer src_buf = mpp_frame_get_buffer(src);
    MppBuffer dst_buf = mpp_frame_get_buffer(dst);
    RK_U32 src_w = mpp_frame_get_width(src);
    RK_U32 src_h = mpp_frame_get_height(src);
    RK_U32 dst_w = mpp_frame_get_width(dst);
    RK_U32 dst_h = mpp_frame_get_height(dst);
    RK_S32 src_fd = mpp_buffer_get_fd(src_buf);
    RK_S32 dst_fd = mpp_buffer_get_fd(dst_buf);
    RgaReq *request = &impl->request;

    RgaFormat src_fmt = rga_fmt_map(mpp_frame_get_fmt(src));
    RgaFormat dst_fmt = rga_fmt_map(mpp_frame_get_fmt(dst));

    rga_dbg_func("in\n");

    if (src_fmt >= RGA_FMT_BUTT || dst_fmt >= RGA_FMT_BUTT) {
        mpp_err("invalid input format for rga process src %d dst %d\n",
                src_fmt, dst_fmt);
        ret = MPP_NOK;
        goto END;
    }

    mpp_assert(src_w > 0 && src_h > 0);

    if (dst_w == 0 || dst_h == 0) {
        dst_w = src_w;
        dst_h = src_h;
    }

    rga_dbg_copy("[fd:w:h:fmt] src - %d:%d:%d:%d dst - %d:%d:%d:%d\n",
                 src_fd, src_w, src_h, src_fmt,
                 dst_fd, dst_w, dst_h, dst_fmt);

    memset(request, 0, sizeof(*request));
    request->src.yrgb_addr = src_fd;
    request->src.format = (RK_U32)src_fmt;
    request->src.vir_w = mpp_frame_get_hor_stride(src);
    request->src.vir_h = mpp_frame_get_ver_stride(src);
    request->src.act_w = src_w;
    request->src.act_h = src_h;

    request->dst.yrgb_addr = dst_fd;
    request->dst.vir_w = dst_w;
    request->dst.vir_h = dst_h;
    request->dst.format = (RK_U32)dst_fmt;
    request->clip.xmin = 0;
    request->clip.xmax = dst_w - 1;
    request->clip.ymin = 0;
    request->clip.ymax = dst_h - 1;
    request->dst.act_w = dst_w;
    request->dst.act_h = dst_h;

    config_rga_yuv2rgb_mode(ctx);

    request->mmu_info.mmu_en = 1;
    request->mmu_info.mmu_flag = 1;
    request->mmu_info.mmu_flag = ((2 & 0x3) << 4) | 1;
    request->mmu_info.mmu_flag |= (1 << 31) | (1 << 10) | (1 << 8);

    ret = rga_ioctl(impl);
END:
    rga_dbg_func("out\n");
    return ret;
}

// sample for duplicate field to frame function
static MPP_RET rga_dup_field_v2(RgaCtx ctx, MppFrame frame)
{
    MPP_RET ret = MPP_OK;
    RgaCtxImpl *impl = (RgaCtxImpl *)ctx;
    MppBuffer buf = mpp_frame_get_buffer(frame);
    RK_U32 width  = mpp_frame_get_width(frame);
    RK_U32 height = mpp_frame_get_height(frame);
    RK_U32 h_str  = mpp_frame_get_hor_stride(frame);
    RK_U32 v_str  = mpp_frame_get_ver_stride(frame);
    RK_S32 fd = mpp_buffer_get_fd(buf);
    void *ptr = mpp_buffer_get_ptr(buf);
    RgaFormat fmt = rga_fmt_map(mpp_frame_get_fmt(frame));
    RgaReq *request = &impl->request;

    rga_dbg_func("in\n");

    mpp_assert(fmt == RGA_FMT_YCbCr_420_SP);
    mpp_assert(width > 0 && height > 0);
    if (fmt != RGA_FMT_YCbCr_420_SP || width == 0 || height == 0) {
        ret = MPP_NOK;
        goto END;
    }

    rga_dbg_dup("[fd:w:h:h_str:v_str:fmt] %d:%d:%d:%d:%d:%d\n",
                fd, width, height, h_str, v_str, fmt);

    memset(request, 0, sizeof(*request));
    request->src.yrgb_addr = fd;
    request->src.format = (RK_U32)fmt;
    request->src.vir_w = h_str * 2;
    request->src.vir_h = v_str / 2;
    request->src.act_w = width;
    request->src.act_h = height / 2;

    request->dst.yrgb_addr = 0;
    request->dst.uv_addr = (RK_U32)((uintptr_t)ptr) + h_str; // special process here
    request->dst.vir_w = h_str * 2;
    request->dst.vir_h = v_str / 2;
    request->dst.format = (RK_U32)fmt;
    request->dst.act_w = width;
    request->dst.act_h = height / 2;

    request->clip.xmin = 0;
    request->clip.xmax = h_str * 2 - 1;
    request->clip.ymin = 0;
    request->clip.ymax = v_str / 2 - 1;

    request->mmu_info.mmu_en = 1;
    request->mmu_info.mmu_flag = ((2 & 0x3) << 4) | 1;
    request->mmu_info.mmu_flag |= (1 << 31) | (1 << 10) | (1 << 8);

    ret = rga_ioctl(impl);
END:
    rga_dbg_func("out\n");
    return ret;
}

// from NormalRgaApi.cpp/NormalRgaLogOutRgaReq
static void RgaLogOutRgaReqV2(RgaReq rgaReg) {
    rga_dbg_func("render_mode = %d rotate_mode = %d\n",
          rgaReg.render_mode, rgaReg.rotate_mode);
    rga_dbg_func("src:[%lx,%lx,%lx],x-y[%d,%d],w-h[%d,%d],vw-vh[%d,%d],f=%d\n",
          (unsigned long)rgaReg.src.yrgb_addr, (unsigned long)rgaReg.src.uv_addr, (unsigned long)rgaReg.src.v_addr,
          rgaReg.src.x_offset, rgaReg.src.y_offset,
          rgaReg.src.act_w, rgaReg.src.act_h,
          rgaReg.src.vir_w, rgaReg.src.vir_h, rgaReg.src.format);
    rga_dbg_func("dst:[%lx,%lx,%lx],x-y[%d,%d],w-h[%d,%d],vw-vh[%d,%d],f=%d\n",
          (unsigned long)rgaReg.dst.yrgb_addr, (unsigned long)rgaReg.dst.uv_addr, (unsigned long)rgaReg.dst.v_addr,
          rgaReg.dst.x_offset, rgaReg.dst.y_offset,
          rgaReg.dst.act_w, rgaReg.dst.act_h,
          rgaReg.dst.vir_w, rgaReg.dst.vir_h, rgaReg.dst.format);
    rga_dbg_func("pat:[%lx,%lx,%lx],x-y[%d,%d],w-h[%d,%d],vw-vh[%d,%d],f=%d\n",
          (unsigned long)rgaReg.pat.yrgb_addr, (unsigned long)rgaReg.pat.uv_addr, (unsigned long)rgaReg.pat.v_addr,
          rgaReg.pat.x_offset, rgaReg.pat.y_offset,
          rgaReg.pat.act_w, rgaReg.pat.act_h,
          rgaReg.pat.vir_w, rgaReg.pat.vir_h, rgaReg.pat.format);
    rga_dbg_func("ROP:[%lx,%x,%x],LUT[%lx]\n",
          (unsigned long)rgaReg.rop_mask_addr, rgaReg.alpha_rop_flag,
          rgaReg.rop_code, (unsigned long)rgaReg.LUT_addr);

    rga_dbg_func("color:[%x,%x,%x,%x,%x]\n",
          rgaReg.color_key_max, rgaReg.color_key_min,
          rgaReg.fg_color, rgaReg.bg_color, rgaReg.color_fill_mode);

    rga_dbg_func("MMU:[%d,%lx,%x]\n", 
          rgaReg.mmu_info.mmu_en, (unsigned long)rgaReg.mmu_info.base_addr, rgaReg.mmu_info.mmu_flag);


    rga_dbg_func("mode[%d,%d,%d,%d,%d]\n", rgaReg.palette_mode, rgaReg.yuv2rgb_mode,
          rgaReg.endian_mode, rgaReg.src_trans_mode,rgaReg.scale_mode);

    return;
}

// compatible
static RgaSURF_FORMAT rga_fmt_map_normalrga(MppFrameFormat fmt)
{
    RgaSURF_FORMAT ret;

    switch (fmt) {
    case MPP_FMT_YUV420P:
        ret = RK_FORMAT_YCbCr_420_P;
        break;
    case MPP_FMT_YUV420SP:
        ret = RK_FORMAT_YCbCr_420_SP;
        break;
    case MPP_FMT_YUV422P:
        ret = RK_FORMAT_YCbCr_422_P;
        break;
    case MPP_FMT_YUV422SP:
        ret = RK_FORMAT_YCrCb_422_SP;
        break;
    case MPP_FMT_RGB565:
        ret = RK_FORMAT_RGB_565;
        break;
    case MPP_FMT_RGB888:
        ret = RK_FORMAT_RGB_888;
        break;
    case MPP_FMT_ARGB8888:
        ret = RK_FORMAT_RGBA_8888;
        break;
    default:
        ret = RK_FORMAT_UNKNOWN;
        mpp_err("unsupport mpp fmt %d found\n", fmt);
        break;
    }

    return ret;
}
static MPP_RET rga_blit_v3(RgaCtx ctx, MppFrame src, MppFrame dst)
{
    MPP_RET ret = MPP_OK;
    // buffer
    MppBuffer src_buf = mpp_frame_get_buffer(src);
    RK_S32 src_fd = mpp_buffer_get_fd(src_buf);
    MppBuffer dst_buf = mpp_frame_get_buffer(dst);
    RK_S32 dst_fd = mpp_buffer_get_fd(dst_buf);
    // rect
    RK_U32 src_x = mpp_frame_get_offset_x(src);
    RK_U32 src_y = mpp_frame_get_offset_y(src);
    RK_U32 src_w = mpp_frame_get_width(src);
    RK_U32 src_h = mpp_frame_get_height(src);
    RK_U32 dst_x = mpp_frame_get_offset_x(dst);
    RK_U32 dst_y = mpp_frame_get_offset_y(dst);
    RK_U32 dst_w = mpp_frame_get_width(dst);
    RK_U32 dst_h = mpp_frame_get_height(dst);
    // stride
    RK_U32 src_hor = mpp_frame_get_hor_stride(src);
    RK_U32 src_ver = mpp_frame_get_ver_stride(src);
    RK_U32 dst_hor = mpp_frame_get_hor_stride(dst);
    RK_U32 dst_ver = mpp_frame_get_ver_stride(dst);

    RgaSURF_FORMAT src_fmt = rga_fmt_map_normalrga(mpp_frame_get_fmt(src));
    RgaSURF_FORMAT dst_fmt = rga_fmt_map_normalrga(mpp_frame_get_fmt(dst));

    rga_dbg_func("in\n");

    if (src_fmt >= RK_FORMAT_UNKNOWN || dst_fmt >= RK_FORMAT_UNKNOWN) {
        mpp_err("invalid input format for rga process src %d dst %d\n", src_fmt, dst_fmt);
        ret = MPP_NOK;
        goto END;
    }

    mpp_assert(src_w > 0 && src_h > 0);

    if (dst_w == 0 || dst_h == 0) {
        dst_w = src_w;
        dst_h = src_h;
    }

    rga_dbg_copy("[fd:w:h:fmt] src - %d:%d:%d:%d dst - %d:%d:%d:%d\n",
                 src_fd, src_w, src_h, src_fmt,
                 dst_fd, dst_w, dst_h, dst_fmt);

    // stride is not same as width for packed format
    //  (hor_stride / bytesPerPixel()) is background width
    /* if (src_x > src_hor) {
        src_x = src_hor;
    }
    if (src_y > src_ver) {
        src_y = src_ver;
    }
    if (src_x + src_w > src_hor) {
        src_w = src_hor - src_x;
    }
    if (src_y + src_h > src_ver) {
        src_h = src_ver - src_y;
    }
    if (dst_x > dst_hor) {
        dst_x = dst_hor;
    }
    if (dst_y > dst_ver) {
        dst_y = dst_ver;
    }
    if (dst_x + dst_w > dst_hor) {
        dst_w = dst_hor - dst_x;
    }
    if (dst_y + dst_h > dst_ver) {
        dst_h = dst_ver - dst_y;
    } */

    rga_info_t src_info;
    memset(&src_info, 0, sizeof(rga_info_t));
    src_info.fd = src_fd;
    src_info.mmuFlag = 1;
    src_info.hnd = -1;
    // src_info.phyAddr = ;
    src_info.virAddr = mpp_buffer_get_ptr(src_buf);
    src_info.sync_mode = RGA_BLIT_SYNC;
    rga_set_rect(&src_info.rect,src_x,src_y,src_w,src_h,src_hor,src_ver,src_fmt);

    rga_info_t dst_info;
    memset(&dst_info, 0, sizeof(rga_info_t));
    dst_info.fd = dst_fd;
    dst_info.mmuFlag = 1;
    dst_info.hnd = -1;
    // dst_info.phyAddr = ;
    dst_info.virAddr = mpp_buffer_get_ptr(dst_buf);
    dst_info.sync_mode = RGA_BLIT_SYNC;
    rga_set_rect(&dst_info.rect,dst_x,dst_y,dst_w,dst_h,dst_hor,dst_ver,dst_fmt);

    ret = (0 == RgaBlit(&src_info, &dst_info, NULL)) ? MPP_OK : MPP_NOK;
END:
    rga_dbg_func("out\n");
    return ret;
}
static MPP_RET rga_copy_v3(RgaCtx ctx, MppFrame src, MppFrame dst)
{
    // stride is not same as width for packed format
    //  (hor_stride / bytesPerPixel()) is background width
    /* mpp_frame_set_offset_x(src, 0);
    mpp_frame_set_offset_y(src, 0);
    mpp_frame_set_width(src, mpp_frame_get_hor_stride(src));
    mpp_frame_set_height(src, mpp_frame_get_ver_stride(src));
    mpp_frame_set_offset_x(dst, 0);
    mpp_frame_set_offset_y(dst, 0);
    mpp_frame_set_width(dst, mpp_frame_get_hor_stride(dst));
    mpp_frame_set_height(dst, mpp_frame_get_ver_stride(dst)); */
    return rga_blit_v3(ctx, src, dst);
}
MPP_RET rga_control(RgaCtx ctx, RgaCmd cmd, void *param)
{
    if (RgaGerVersion() == 1) {
        return MPP_NOK; // not support
    } else if (RgaGerVersion() == 2) {
        return rga_control_v2(ctx, cmd, param);
    } else if (RgaGerVersion() == 3) {
        return MPP_NOK; // not support
    } else {
        return MPP_NOK; // something failed
    }
}
MPP_RET rga_copy(RgaCtx ctx, MppFrame src, MppFrame dst)
{
    if (RgaGerVersion() == 1) {
        return MPP_NOK; // not support
    } else if (RgaGerVersion() == 2) {
        return rga_copy_v2(ctx, src, dst);
    } else if (RgaGerVersion() == 3) {
        return rga_copy_v3(ctx, src, dst);
    } else {
        return MPP_NOK; // something failed
    }
}
MPP_RET rga_dup_field(RgaCtx ctx, MppFrame frame)
{
    if (RgaGerVersion() == 1) {
        return MPP_NOK; // not support
    } else if (RgaGerVersion() == 2) {
        return rga_dup_field_v2(ctx, frame);
    } else if (RgaGerVersion() == 3) {
        return MPP_NOK; // not support
    } else {
        return MPP_NOK; // something failed
    }
}
MPP_RET rga_blit(RgaCtx ctx, MppFrame src, MppFrame dst)
{
    if (RgaGerVersion() == 1) {
        return MPP_NOK; // not support
    } else if (RgaGerVersion() == 2) {
        return MPP_NOK; // not support
    } else if (RgaGerVersion() == 3) {
        return rga_blit_v3(ctx, src, dst);
    } else {
        return MPP_NOK; // something failed
    }
}
