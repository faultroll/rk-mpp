/*
 * Copyright (C) 2016 Rockchip Electronics Co., Ltd.
 * Authors:
 *  Zhiqin Wei <wzq@rock-chips.com>
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
#include "NormalRga.h"
#include "NormalRgaContext.h"

#include <sys/ioctl.h>
#include <pthread.h>

pthread_mutex_t mMutex = PTHREAD_MUTEX_INITIALIZER;

#define RGA_SRCOVER_EN 1

volatile int32_t refCount = 0;
struct rgaContext *rgaCtx = NULL;

void NormalRgaSetLogOnceFlag(int log) {
    struct rgaContext *ctx = NULL;

    ctx->mLogOnce = log;
    return;
}

void NormalRgaSetAlwaysLogFlag(int log) {
    struct rgaContext *ctx = NULL;

    ctx->mLogAlways = log;
    return;
}

int NormalRgaOpen(void **context) {
    struct rgaContext *ctx = NULL;
    char buf[30];
    int fd = -1;
    int ret = 0;

    if (!context) {
        ret = -EINVAL;
        goto mallocErr;
    }

    if (!rgaCtx) {
        ctx = (struct rgaContext *)malloc(sizeof(struct rgaContext));
        if(!ctx) {
            ret = -ENOMEM;
            ALOGE("malloc fail:%s.",strerror(errno));
            goto mallocErr;
        }

        fd = open("/dev/rga", O_RDWR, 0);
        if (fd < 0) {
            ret = -ENODEV;
            ALOGE("failed to open RGA:%s.",strerror(errno));
            goto rgaOpenErr;
        }
        ctx->rgaFd = fd;

        ret = ioctl(fd, RGA_IOC_GET_DRVIER_VERSION, &ctx->mDriverVersion);
        if (ret >= 0) {
            ctx->mRgaVer = 3;

            ret = ioctl(fd, RGA_IOC_GET_HW_VERSION, &ctx->mHwVersions);
            if (ret < 0) {
                ALOGE("librga fail to get hw versions!");
                goto getVersionError;
            }

            /*
             * For legacy: Because normalRGA requires a version greater
             *             than 2.0 to use rga2 normally.
             */
            ctx->mVersion = (float)3.2;
        } else {
            ALOGE("librga fail to get driver version! Legacy mode will be enabled.");

            /* Choose legacy mode. */
            ctx->mHwVersions.size = 1;
            /* Try to get the version of RGA2 */
            ret = ioctl(fd, RGA2_GET_VERSION, ctx->mHwVersions.version[0].str);
            if (ret < 0) {
                /* Try to get the version of RGA1 */
                ret = ioctl(fd, RGA_GET_VERSION, ctx->mHwVersions.version[0].str);
                if (ret < 0) {
                    ALOGE("librga fail to get RGA2/RGA1 version! %s", strerror(ret));
                    goto getVersionError;
                } else {
                    ctx->mRgaVer = 1;
                }
            } else {
                ctx->mRgaVer = 2;
            }

            sscanf((char *)ctx->mHwVersions.version[0].str, "%x.%x.%x",
                &ctx->mHwVersions.version[0].major,
                &ctx->mHwVersions.version[0].minor,
                &ctx->mHwVersions.version[0].revision);

            ctx->mVersion = atof((char *)ctx->mHwVersions.version[0].str);
        }
        ALOGE("rga mRgaVer = %d, mVersion = %f", ctx->mRgaVer, ctx->mVersion);

        NormalRgaInitTables();

        rgaCtx = ctx;
    } else {
        ctx = rgaCtx;
        ALOGE("Had init the rga dev ctx = %p, mRgaVer = %d, mVersion = %f", ctx, ctx->mRgaVer, ctx->mVersion);
    }

    pthread_mutex_lock(&mMutex);
    refCount++;
    pthread_mutex_unlock(&mMutex);

    *context = (void *)ctx;
    return ret;

getVersionError:
rgaOpenErr:
    free(ctx);
mallocErr:
    return ret;
}

int NormalRgaClose(void **context) {
    struct rgaContext *ctx = rgaCtx;

    if (!ctx) {
        ALOGE("Try to exit uninit rgaCtx=%p", ctx);
        return -ENODEV;
    }

    if (!*context) {
        ALOGE("Try to uninit rgaCtx=%p", *context);
        return -ENODEV;
    }

    if (*context != ctx) {
        ALOGE("Try to exit wrong ctx=%p",ctx);
        return -ENODEV;
    }

    if (refCount <= 0) {
        ALOGE("This can not be happened, close before init");
        return 0;
    }

    pthread_mutex_lock(&mMutex);
    refCount--;

    if (refCount < 0) {
        refCount = 0;
        pthread_mutex_unlock(&mMutex);
        return 0;
    }

    if (refCount > 0)
    {
        pthread_mutex_unlock(&mMutex);
        return 0;
    }

    pthread_mutex_unlock(&mMutex);

    rgaCtx = NULL;
    *context = NULL;

    close(ctx->rgaFd);

    free(ctx);

    return 0;
}

int RgaInit(void **ctx) {
    int ret = 0;
    ret = NormalRgaOpen(ctx);
    return ret;
}

int RgaDeInit(void **ctx) {
    int ret = 0;
    ret = NormalRgaClose(ctx);
    return ret;
}

int RgaGerVersion() {
    struct rgaContext *ctx = rgaCtx;
    if (!ctx) {
        ALOGE("Try to use uninit rgaCtx=%p",ctx);
        return -ENODEV;
    }
    return ctx->mRgaVer;
}

int RgaBlit(rga_info_t *src, rga_info_t *dst, rga_info_t *src1) {
    //check rects
    //check buffer_handle_t with rects
    struct rgaContext *ctx = rgaCtx;
    int srcVirW,srcVirH,srcActW,srcActH,srcXPos,srcYPos;
    int dstVirW,dstVirH,dstActW,dstActH,dstXPos,dstYPos;
    int src1VirW,src1VirH,src1ActW,src1ActH,src1XPos,src1YPos;
    int scaleMode,rotateMode,orientation,ditherEn;
    int srcType,dstType,src1Type,srcMmuFlag,dstMmuFlag,src1MmuFlag;
    int planeAlpha;
    int dstFd = -1;
    int srcFd = -1;
    int src1Fd = -1;
    int rotation;
    int stretch = 0;
    float hScale = 1;
    float vScale = 1;
    int ret = 0;
    rga_rect_t relSrcRect,tmpSrcRect,relDstRect,tmpDstRect;
    rga_rect_t relSrc1Rect,tmpSrc1Rect;
    struct rga_req rgaReg,tmprgaReg;
    unsigned int blend;
    unsigned int yuvToRgbMode;
    bool perpixelAlpha = 0;
    void *srcBuf = NULL;
    void *dstBuf = NULL;
    void *src1Buf = NULL;
    RECT clip;
    int sync_mode = RGA_BLIT_SYNC;

    //init context
    if (!ctx) {
        ALOGE("Try to use uninit rgaCtx=%p",ctx);
        return -ENODEV;
    }

    //init
    memset(&rgaReg, 0, sizeof(struct rga_req));

    srcType = dstType = srcMmuFlag = dstMmuFlag = 0;
    src1Type = src1MmuFlag = 0;
    rotation = 0;
    blend = 0;
    yuvToRgbMode = 0;

    if (!src && !dst && !src1) {
        ALOGE("src = %p, dst = %p, src1 = %p", src, dst, src1);
        return -EINVAL;
    }

    if (!src && !dst) {
        ALOGE("src = %p, dst = %p", src, dst);
        return -EINVAL;
    }

    /*
     * 1.if src exist, get some parameter from src, such as rotatiom.
     * 2.if need to blend, need blend variable from src to decide how to blend.
     * 3.get effective area from src, if the area is empty, choose to get parameter from handle.
     * */
    if (src) {
        rotation = src->rotation;
        blend = src->blend;
        memcpy(&relSrcRect, &src->rect, sizeof(rga_rect_t));
    }

    /* get effective area from dst and src1, if the area is empty, choose to get parameter from handle. */
    if (dst)
        memcpy(&relDstRect, &dst->rect, sizeof(rga_rect_t));
    if (src1)
        memcpy(&relSrc1Rect, &src1->rect, sizeof(rga_rect_t));

    srcFd = dstFd = src1Fd = -1;

    if (src1) {
        if (src->handle > 0 && dst->handle > 0 && src1->handle > 0) {
            if (src->handle <= 0 || dst->handle <= 0 || src1->handle <= 0) {
                ALOGE("librga only supports the use of handles only or no handles, [src,src1,dst] = [%d, %d, %d]",
                      src->handle, src1->handle, dst->handle);
                return -EINVAL;
            }

            /* This will mark the use of handle */
            rgaReg.handle_flag |= 1;
        }
    } else if (src->handle > 0 && dst->handle > 0) {
        if (src->handle <= 0 || dst->handle <= 0) {
            ALOGE("librga only supports the use of handles only or no handles, [src,dst] = [%d, %d]",
                  src->handle, dst->handle);
            return -EINVAL;
        }

        /* This will mark the use of handle */
        rgaReg.handle_flag |= 1;
    }

    /*********** get src addr *************/
    if (src && src->handle) {
        /* In order to minimize changes, the handle here will reuse the variable of Fd. */
        srcFd = src->handle;
    } else if (src && src->phyAddr) {
        srcBuf = src->phyAddr;
    } else if (src && src->fd > 0) {
        srcFd = src->fd;
        src->mmuFlag = 1;
    } else if (src && src->virAddr) {
        srcBuf = src->virAddr;
        src->mmuFlag = 1;
    }
    /*
     * After getting the fd or virtual address through the handle,
     * set 'srcType' to 1, and at the end, and then judge
     * the 'srcType' at the end whether to enable mmu.
     */
    if (srcFd == -1 && !srcBuf) {
        ALOGE("%d:src has not fd and address for render", __LINE__);
        return ret;
    }
    if (srcFd == 0 && !srcBuf) {
        ALOGE("srcFd is zero, now driver not support");
        return -EINVAL;
    }
    /* Old rga driver cannot support fd as zero. */
    if (srcFd == 0)
        srcFd = -1;

    /*********** get src1 addr *************/
    if (src1) {
        if (src1 && src1->handle) {
            /* In order to minimize changes, the handle here will reuse the variable of Fd. */
            src1Fd = src1->handle;
        } else if (src1 && src1->phyAddr) {
            src1Buf = src1->phyAddr;
        } else if (src1 && src1->fd > 0) {
            src1Fd = src1->fd;
            src1->mmuFlag = 1;
        } else if (src1 && src1->virAddr) {
            src1Buf = src1->virAddr;
            src1->mmuFlag = 1;
        }
        /*
         * After getting the fd or virtual address through the handle,
         * set 'src1Type' to 1, and at the end, and then judge
         * the 'src1Type' at the end whether to enable mmu.
         */
        if (src1Fd == -1 && !src1Buf) {
            ALOGE("%d:src1 has not fd and address for render", __LINE__);
            return ret;
        }
        if (src1Fd == 0 && !src1Buf) {
            ALOGE("src1Fd is zero, now driver not support");
            return -EINVAL;
        }
        /* Old rga driver cannot support fd as zero. */
        if (src1Fd == 0)
            src1Fd = -1;
    }

    /*********** get dst addr *************/
    if (dst && dst->handle) {
        /* In order to minimize changes, the handle here will reuse the variable of Fd. */
        dstFd = dst->handle;
    } else if (dst && dst->phyAddr) {
        dstBuf = dst->phyAddr;
    } else if (dst && dst->fd > 0) {
        dstFd = dst->fd;
        dst->mmuFlag = 1;
    } else if (dst && dst->virAddr) {
        dstBuf = dst->virAddr;
        dst->mmuFlag = 1;
    }
    /*
     * After getting the fd or virtual address through the handle,
     * set 'dstType' to 1, and at the end, and then judge
     * the 'dstType' at the end whether to enable mmu.
     */
    if (dstFd == -1 && !dstBuf) {
        ALOGE("%d:dst has not fd and address for render", __LINE__);
        return ret;
    }
    if (dstFd == 0 && !dstBuf) {
        ALOGE("dstFd is zero, now driver not support");
        return -EINVAL;
    }
    /* Old rga driver cannot support fd as zero. */
    if (dstFd == 0)
        dstFd = -1;

    relSrcRect.format = RkRgaCompatibleFormat(relSrcRect.format);
    relDstRect.format = RkRgaCompatibleFormat(relDstRect.format);
    if (isRectValid(relSrc1Rect))
        relSrc1Rect.format = RkRgaCompatibleFormat(relSrc1Rect.format);

#ifdef RK3126C
    if ( (relSrcRect.width == relDstRect.width) && (relSrcRect.height == relDstRect.height ) &&
         (relSrcRect.width + 2*relSrcRect.xoffset == relSrcRect.wstride) &&
         (relSrcRect.height + 2*relSrcRect.yoffset == relSrcRect.hstride) &&
         (relSrcRect.format == HAL_PIXEL_FORMAT_YCrCb_NV12) && (relSrcRect.xoffset > 0 && relSrcRect.yoffset > 0)
       ) {
        relSrcRect.width += 4;
        //relSrcRect.height += 4;
        relSrcRect.xoffset = (relSrcRect.wstride - relSrcRect.width) / 2;
    }
#endif

    /* blend bit[16:23] is to set global alpha. */
    planeAlpha = (blend & 0xFF0000) >> 16;

    /* determined by format, need pixel alpha or not. */
    perpixelAlpha = NormalRgaFormatHasAlpha(RkRgaGetRgaFormat(relSrcRect.format));

    /* blend bit[0:15] is to set which way to blend,such as whether need glabal alpha,and so on. */
    switch ((blend & 0xFFFF)) {
        case 0x0001:/* src */
            NormalRgaSetAlphaEnInfo(&rgaReg, 1, 2, planeAlpha , 1, 1, 0);
            break;

        case 0x0002:/* dst */
            NormalRgaSetAlphaEnInfo(&rgaReg, 1, 2, planeAlpha , 1, 2, 0);
            break;

        case 0x0105:/* src over , no need to Premultiplied. */
            if (perpixelAlpha && planeAlpha < 255) {
                NormalRgaSetAlphaEnInfo(&rgaReg, 1, 2, planeAlpha, 1, 9, 0);
            } else if (perpixelAlpha)
                NormalRgaSetAlphaEnInfo(&rgaReg, 1, 1, 0, 1, 3, 0);
            else
                NormalRgaSetAlphaEnInfo(&rgaReg, 1, 0, planeAlpha, 0, 0, 0);
            break;

        case 0x0405:/* src over , need to Premultiplied. */
            if (perpixelAlpha && planeAlpha < 255)
                NormalRgaSetAlphaEnInfo(&rgaReg, 1, 2, planeAlpha, 1, 9, 0);
            else if (perpixelAlpha)
                NormalRgaSetAlphaEnInfo(&rgaReg, 1, 1, 0, 1, 3, 0);
            else
                NormalRgaSetAlphaEnInfo(&rgaReg, 1, 0, planeAlpha, 0, 0, 0);

            rgaReg.alpha_rop_flag |= (1 << 9);  //real color mode

            break;

        case 0x0501:/* dst over , no need premultiplied. */
            NormalRgaSetAlphaEnInfo(&rgaReg, 1, 2, planeAlpha , 1, 4, 0);
            break;

        case 0x0504:/* dst over, need premultiplied. */
            NormalRgaSetAlphaEnInfo(&rgaReg, 1, 2, planeAlpha , 1, 4, 0);
            rgaReg.alpha_rop_flag |= (1 << 9);  //real color mode
            break;

        case 0x0100:
        default:
            /* Tips: BLENDING_NONE is non-zero value, handle zero value as
             * BLENDING_NONE. */
            /* C = Cs
             * A = As */
            break;
    }

    /* discripe a picture need high stride.If high stride not to be set, need use height as high stride. */
    if (relSrcRect.hstride == 0)
        relSrcRect.hstride = relSrcRect.height;

    if (isRectValid(relSrc1Rect))
        if (relSrc1Rect.hstride == 0)
            relSrc1Rect.hstride = relSrc1Rect.height;

    if (relDstRect.hstride == 0)
        relDstRect.hstride = relDstRect.height;

    /* do some check, check the area of src and dst whether is effective. */
    if (src) {
        ret = checkRectForRga(relSrcRect);
        if (ret) {
            ALOGE("[%s,%d]Error srcRect ", __func__, __LINE__);
            return ret;
        }
    }

    if (src1) {
        ret = checkRectForRga(relSrc1Rect);
        if (ret) {
            ALOGE("[%s,%d]Error src1Rect ", __func__, __LINE__);
            return ret;
        }
    }

    if (dst) {
        ret = checkRectForRga(relDstRect);
        if (ret) {
            ALOGE("[%s,%d]Error dstRect ", __func__, __LINE__);
            return ret;
        }
    }

    /* check the scale magnification. */
    if (src1 && src) {
        hScale = (float)relSrcRect.width / relSrc1Rect.width;
        vScale = (float)relSrcRect.height / relSrc1Rect.height;
        if (rotation == HAL_TRANSFORM_ROT_90 || rotation == HAL_TRANSFORM_ROT_270) {
            hScale = (float)relSrcRect.width / relSrc1Rect.height;
            vScale = (float)relSrcRect.height / relSrc1Rect.width;
        }
        if (hScale < 1/16 || hScale > 16 || vScale < 1/16 || vScale > 16) {
            ALOGE("Error scale[%f,%f] line %d", hScale, vScale, __LINE__);
            return -EINVAL;
        }
        if (ctx->mVersion <= 2.0 && (hScale < 1/8 ||
                                     hScale > 8 || vScale < 1/8 || vScale > 8)) {
            ALOGE("Error scale[%f,%f] line %d", hScale, vScale, __LINE__);
            return -EINVAL;
        }
        if (ctx->mVersion <= 1.003 && (hScale < 1/2 || vScale < 1/2)) {
            ALOGE("e scale[%f,%f] ver[%f]", hScale, vScale, ctx->mVersion);
            return -EINVAL;
        }
    } else if (src && dst) {
        hScale = (float)relSrcRect.width / relDstRect.width;
        vScale = (float)relSrcRect.height / relDstRect.height;
        if (rotation == HAL_TRANSFORM_ROT_90 || rotation == HAL_TRANSFORM_ROT_270) {
            hScale = (float)relSrcRect.width / relDstRect.height;
            vScale = (float)relSrcRect.height / relDstRect.width;
        }
        if (hScale < 1.0/16 || hScale > 16 || vScale < 1.0/16 || vScale > 16) {
            ALOGE("Error scale[%f,%f] line %d", hScale, vScale, __LINE__);
            return -EINVAL;
        }
        if (ctx->mVersion < 2.0 && (hScale < 1.0/8 ||
                                       hScale > 8 || vScale < 1.0/8 || vScale > 8)) {
            ALOGE("Error scale[%f,%f] line %d", hScale, vScale, __LINE__);
            return -EINVAL;
        }
        if (ctx->mVersion <= 1.003 && (hScale < 1.0/2 || vScale < 1.0/2)) {
            ALOGE("e scale[%f,%f] ver[%f]", hScale, vScale, ctx->mVersion);
            return -EINVAL;
        }
    }

    /* reselect the scale mode. */
    scaleMode = 0;
    stretch = (hScale != 1.0f) || (vScale != 1.0f);
    /* scale up use bicubic */
    if (hScale < 1 || vScale < 1) {
        scaleMode = 2;
        if((relSrcRect.format == RK_FORMAT_RGBA_8888  || relSrcRect.format == RK_FORMAT_BGRA_8888))
            scaleMode = 0;     //  force change scale_mode to 0 ,for rga not support
    }

    /*
     * according to the rotation to set corresponding parameter.It's diffrient from the opengl.
     * Following's config which use frequently
     * */
    switch (rotation & 0x0f) {
        case HAL_TRANSFORM_FLIP_H:
            orientation = 0;
            rotateMode = 2;
            srcVirW = relSrcRect.wstride;
            srcVirH = relSrcRect.hstride;
            srcXPos = relSrcRect.xoffset;
            srcYPos = relSrcRect.yoffset;
            srcActW = relSrcRect.width;
            srcActH = relSrcRect.height;

            src1VirW = relSrc1Rect.wstride;
            src1VirH = relSrc1Rect.hstride;
            src1XPos = relSrc1Rect.xoffset;
            src1YPos = relSrc1Rect.yoffset;
            src1ActW = relSrc1Rect.width;
            src1ActH = relSrc1Rect.height;

            dstVirW = relDstRect.wstride;
            dstVirH = relDstRect.hstride;
            dstXPos = relDstRect.xoffset;
            dstYPos = relDstRect.yoffset;
            dstActW = relDstRect.width;
            dstActH = relDstRect.height;
            break;
        case HAL_TRANSFORM_FLIP_V:
            orientation = 0;
            rotateMode = 3;
            srcVirW = relSrcRect.wstride;
            srcVirH = relSrcRect.hstride;
            srcXPos = relSrcRect.xoffset;
            srcYPos = relSrcRect.yoffset;
            srcActW = relSrcRect.width;
            srcActH = relSrcRect.height;

            src1VirW = relSrc1Rect.wstride;
            src1VirH = relSrc1Rect.hstride;
            src1XPos = relSrc1Rect.xoffset;
            src1YPos = relSrc1Rect.yoffset;
            src1ActW = relSrc1Rect.width;
            src1ActH = relSrc1Rect.height;

            dstVirW = relDstRect.wstride;
            dstVirH = relDstRect.hstride;
            dstXPos = relDstRect.xoffset;
            dstYPos = relDstRect.yoffset;
            dstActW = relDstRect.width;
            dstActH = relDstRect.height;
            break;
        case HAL_TRANSFORM_FLIP_H_V:
            orientation = 0;
            rotateMode = 4;
            srcVirW = relSrcRect.wstride;
            srcVirH = relSrcRect.hstride;
            srcXPos = relSrcRect.xoffset;
            srcYPos = relSrcRect.yoffset;
            srcActW = relSrcRect.width;
            srcActH = relSrcRect.height;

            src1VirW = relSrc1Rect.wstride;
            src1VirH = relSrc1Rect.hstride;
            src1XPos = relSrc1Rect.xoffset;
            src1YPos = relSrc1Rect.yoffset;
            src1ActW = relSrc1Rect.width;
            src1ActH = relSrc1Rect.height;

            dstVirW = relDstRect.wstride;
            dstVirH = relDstRect.hstride;
            dstXPos = relDstRect.xoffset;
            dstYPos = relDstRect.yoffset;
            dstActW = relDstRect.width;
            dstActH = relDstRect.height;
            break;
        case HAL_TRANSFORM_ROT_90:
            orientation = 90;
            rotateMode = 1;
            srcVirW = relSrcRect.wstride;
            srcVirH = relSrcRect.hstride;
            srcXPos = relSrcRect.xoffset;
            srcYPos = relSrcRect.yoffset;
            srcActW = relSrcRect.width;
            srcActH = relSrcRect.height;

            src1VirW = relSrc1Rect.wstride;
            src1VirH = relSrc1Rect.hstride;
            src1XPos = relSrc1Rect.xoffset;
            src1YPos = relSrc1Rect.yoffset;
            src1ActW = relSrc1Rect.height;
            src1ActH = relSrc1Rect.width;

            dstVirW = relDstRect.wstride;
            dstVirH = relDstRect.hstride;
            dstXPos = relDstRect.xoffset;
            dstYPos = relDstRect.yoffset;
            dstActW = relDstRect.height;
            dstActH = relDstRect.width;
            break;
        case HAL_TRANSFORM_ROT_180:
            orientation = 180;
            rotateMode = 1;
            srcVirW = relSrcRect.wstride;
            srcVirH = relSrcRect.hstride;
            srcXPos = relSrcRect.xoffset;
            srcYPos = relSrcRect.yoffset;
            srcActW = relSrcRect.width;
            srcActH = relSrcRect.height;

            src1VirW = relSrc1Rect.wstride;
            src1VirH = relSrc1Rect.hstride;
            src1XPos = relSrc1Rect.xoffset;
            src1YPos = relSrc1Rect.yoffset;
            src1ActW = relSrc1Rect.width;
            src1ActH = relSrc1Rect.height;

            dstVirW = relDstRect.wstride;
            dstVirH = relDstRect.hstride;
            dstXPos = relDstRect.xoffset;
            dstYPos = relDstRect.yoffset;
            dstActW = relDstRect.width;
            dstActH = relDstRect.height;
            break;
        case HAL_TRANSFORM_ROT_270:
            orientation = 270;
            rotateMode = 1;
            srcVirW = relSrcRect.wstride;
            srcVirH = relSrcRect.hstride;
            srcXPos = relSrcRect.xoffset;
            srcYPos = relSrcRect.yoffset;
            srcActW = relSrcRect.width;
            srcActH = relSrcRect.height;

            src1VirW = relSrc1Rect.wstride;
            src1VirH = relSrc1Rect.hstride;
            src1XPos = relSrc1Rect.xoffset;
            src1YPos = relSrc1Rect.yoffset;
            src1ActW = relSrc1Rect.height;
            src1ActH = relSrc1Rect.width;

            dstVirW = relDstRect.wstride;
            dstVirH = relDstRect.hstride;
            dstXPos = relDstRect.xoffset;
            dstYPos = relDstRect.yoffset;
            dstActW = relDstRect.height;
            dstActH = relDstRect.width;
            break;
        default:
            orientation = 0;
            rotateMode = stretch;
            srcVirW = relSrcRect.wstride;
            srcVirH = relSrcRect.hstride;
            srcXPos = relSrcRect.xoffset;
            srcYPos = relSrcRect.yoffset;
            srcActW = relSrcRect.width;
            srcActH = relSrcRect.height;

            src1VirW = relSrc1Rect.wstride;
            src1VirH = relSrc1Rect.hstride;
            src1XPos = relSrc1Rect.xoffset;
            src1YPos = relSrc1Rect.yoffset;
            src1ActW = relSrc1Rect.width;
            src1ActH = relSrc1Rect.height;

            dstVirW = relDstRect.wstride;
            dstVirH = relDstRect.hstride;
            dstXPos = relDstRect.xoffset;
            dstYPos = relDstRect.yoffset;
            dstActW = relDstRect.width;
            dstActH = relDstRect.height;
            break;
    }

    switch ((rotation & 0xF0) >> 4) {
        case HAL_TRANSFORM_FLIP_H :
            rotateMode |= (2 << 4);
            break;
        case HAL_TRANSFORM_FLIP_V :
            rotateMode |= (3 << 4);
            break;
        case HAL_TRANSFORM_FLIP_H_V:
            rotateMode |= (4 << 4);
            break;
    }

    /* if pictual out of range should be cliped. */
    clip.xmin = 0;
    clip.xmax = dstVirW - 1;
    clip.ymin = 0;
    clip.ymax = dstVirH - 1;

    if  (NormalRgaIsRgbFormat(RkRgaGetRgaFormat(relSrcRect.format)) &&
         (RkRgaGetRgaFormat(relSrcRect.format) != RK_FORMAT_RGB_565 ||
         RkRgaGetRgaFormat(relSrcRect.format) != RK_FORMAT_BGR_565) &&
         (RkRgaGetRgaFormat(relDstRect.format) == RK_FORMAT_RGB_565 ||
         RkRgaGetRgaFormat(relDstRect.format) == RK_FORMAT_BGR_565))
        ditherEn = 1;
    else
        ditherEn = 0;

    /* YUV HDS or VDS enable */
    if (NormalRgaIsYuvFormat(relDstRect.format)) {
        rgaReg.uvhds_mode = 1;
        if ((relDstRect.format == RK_FORMAT_YCbCr_420_SP ||
             relDstRect.format == RK_FORMAT_YCrCb_420_SP) &&
            rotation == 0 && hScale == 1.0f && vScale == 1.0f) {
            /* YUV420SP only support vds when without rotation and scale. */
            rgaReg.uvvds_mode = 1;
        }
    }

    /* only to configure the parameter by driver version, because rga driver has too many version. */
    if (ctx->mVersion <= (float)1.003) {
        srcMmuFlag = dstMmuFlag = src1MmuFlag = 1;

#if defined(__arm64__) || defined(__aarch64__)
        NormalRgaSetSrcVirtualInfo(&rgaReg, (unsigned long)srcBuf,
                                   (unsigned long)srcBuf + srcVirW * srcVirH,
                                   (unsigned long)srcBuf + srcVirW * srcVirH * 5/4,
                                   srcVirW, srcVirH,
                                   RkRgaGetRgaFormat(relSrcRect.format),0);
        /* src1 */
        if (src1)
            NormalRgaSetPatVirtualInfo(&rgaReg, (unsigned long)src1Buf,
                                       (unsigned long)src1Buf + src1VirW * src1VirH,
                                       (unsigned long)src1Buf + src1VirW * src1VirH * 5/4,
                                       src1VirW, src1VirH, &clip,
                                       RkRgaGetRgaFormat(relSrc1Rect.format),0);
        /*dst*/
        NormalRgaSetDstVirtualInfo(&rgaReg, (unsigned long)dstBuf,
                                   (unsigned long)dstBuf + dstVirW * dstVirH,
                                   (unsigned long)dstBuf + dstVirW * dstVirH * 5/4,
                                   dstVirW, dstVirH, &clip,
                                   RkRgaGetRgaFormat(relDstRect.format),0);
#else
        NormalRgaSetSrcVirtualInfo(&rgaReg, (unsigned long)srcBuf,
                                   (unsigned int)srcBuf + srcVirW * srcVirH,
                                   (unsigned int)srcBuf + srcVirW * srcVirH * 5/4,
                                   srcVirW, srcVirH,
                                   RkRgaGetRgaFormat(relSrcRect.format),0);
        /* src1 */
        if (src1)
            NormalRgaSetPatVirtualInfo(&rgaReg, (unsigned long)src1Buf,
                                       (unsigned int)src1Buf + src1VirW * src1VirH,
                                       (unsigned int)src1Buf + src1VirW * src1VirH * 5/4,
                                       src1VirW, src1VirH, &clip,
                                       RkRgaGetRgaFormat(relSrc1Rect.format),0);
        /*dst*/
        NormalRgaSetDstVirtualInfo(&rgaReg, (unsigned long)dstBuf,
                                   (unsigned int)dstBuf + dstVirW * dstVirH,
                                   (unsigned int)dstBuf + dstVirW * dstVirH * 5/4,
                                   dstVirW, dstVirH, &clip,
                                   RkRgaGetRgaFormat(relDstRect.format),0);

#endif
        /* the version 1.005 is different to assign fd from version 2.0 and above */
    } else if (ctx->mVersion < (float)1.6) {
        /*Src*/
        if (srcFd != -1) {
            srcMmuFlag = srcType ? 1 : 0;
            if (src && srcFd == src->fd)
                srcMmuFlag = src->mmuFlag ? 1 : 0;
            NormalRgaSetSrcVirtualInfo(&rgaReg, 0, 0, 0, srcVirW, srcVirH,
                                       RkRgaGetRgaFormat(relSrcRect.format),0);
            NormalRgaSetFdsOffsets(&rgaReg, srcFd, 0, 0, 0);
        } else {
            if (src && src->hnd)
                srcMmuFlag = srcType ? 1 : 0;
            if (src && srcBuf == src->virAddr)
                srcMmuFlag = 1;
            if (src && srcBuf == src->phyAddr)
                srcMmuFlag = 0;
#if defined(__arm64__) || defined(__aarch64__)
            NormalRgaSetSrcVirtualInfo(&rgaReg, (unsigned long)srcBuf,
                                       (unsigned long)srcBuf + srcVirW * srcVirH,
                                       (unsigned long)srcBuf + srcVirW * srcVirH * 5/4,
                                       srcVirW, srcVirH,
                                       RkRgaGetRgaFormat(relSrcRect.format),0);
#else
            NormalRgaSetSrcVirtualInfo(&rgaReg, (unsigned int)srcBuf,
                                       (unsigned int)srcBuf + srcVirW * srcVirH,
                                       (unsigned int)srcBuf + srcVirW * srcVirH * 5/4,
                                       srcVirW, srcVirH,
                                       RkRgaGetRgaFormat(relSrcRect.format),0);
#endif
        }
        /* src1 */
        if (src1) {
            if (src1Fd != -1) {
                src1MmuFlag = src1Type ? 1 : 0;
                if (src1 && src1Fd == src1->fd)
                    src1MmuFlag = src1->mmuFlag ? 1 : 0;
                NormalRgaSetPatVirtualInfo(&rgaReg, 0, 0, 0, src1VirW, src1VirH, &clip,
                                           RkRgaGetRgaFormat(relSrc1Rect.format),0);
                /*src dst fd*/
                NormalRgaSetFdsOffsets(&rgaReg, 0, src1Fd, 0, 0);
            } else {
                if (src1 && src1->hnd)
                    src1MmuFlag = src1Type ? 1 : 0;
                if (src1 && src1Buf == src1->virAddr)
                    src1MmuFlag = 1;
                if (src1 && src1Buf == src1->phyAddr)
                    src1MmuFlag = 0;
#if defined(__arm64__) || defined(__aarch64__)
                NormalRgaSetPatVirtualInfo(&rgaReg, (unsigned long)src1Buf,
                                           (unsigned long)src1Buf + src1VirW * src1VirH,
                                           (unsigned long)src1Buf + src1VirW * src1VirH * 5/4,
                                           src1VirW, src1VirH, &clip,
                                           RkRgaGetRgaFormat(relSrc1Rect.format),0);
#else
                NormalRgaSetPatVirtualInfo(&rgaReg, (unsigned int)src1Buf,
                                           (unsigned int)src1Buf + src1VirW * src1VirH,
                                           (unsigned int)src1Buf + src1VirW * src1VirH * 5/4,
                                           src1VirW, src1VirH, &clip,
                                           RkRgaGetRgaFormat(relSrc1Rect.format),0);
#endif
            }
        }
        /*dst*/
        if (dstFd != -1) {
            dstMmuFlag = dstType ? 1 : 0;
            if (dst && dstFd == dst->fd)
                dstMmuFlag = dst->mmuFlag ? 1 : 0;
            NormalRgaSetDstVirtualInfo(&rgaReg, 0, 0, 0, dstVirW, dstVirH, &clip,
                                       RkRgaGetRgaFormat(relDstRect.format),0);
            /*src dst fd*/
            NormalRgaSetFdsOffsets(&rgaReg, 0, dstFd, 0, 0);
        } else {
            if (dst && dst->hnd)
                dstMmuFlag = dstType ? 1 : 0;
            if (dst && dstBuf == dst->virAddr)
                dstMmuFlag = 1;
            if (dst && dstBuf == dst->phyAddr)
                dstMmuFlag = 0;
#if defined(__arm64__) || defined(__aarch64__)
            NormalRgaSetDstVirtualInfo(&rgaReg, (unsigned long)dstBuf,
                                       (unsigned long)dstBuf + dstVirW * dstVirH,
                                       (unsigned long)dstBuf + dstVirW * dstVirH * 5/4,
                                       dstVirW, dstVirH, &clip,
                                       RkRgaGetRgaFormat(relDstRect.format),0);
#else
            NormalRgaSetDstVirtualInfo(&rgaReg, (unsigned int)dstBuf,
                                       (unsigned int)dstBuf + dstVirW * dstVirH,
                                       (unsigned int)dstBuf + dstVirW * dstVirH * 5/4,
                                       dstVirW, dstVirH, &clip,
                                       RkRgaGetRgaFormat(relDstRect.format),0);
#endif
        }
    } else {
        if (src && src->hnd)
            srcMmuFlag = srcType ? 1 : 0;
        if (src && srcBuf == src->virAddr)
            srcMmuFlag = 1;
        if (src && srcBuf == src->phyAddr)
            srcMmuFlag = 0;
        if (srcFd != -1)
            srcMmuFlag = srcType ? 1 : 0;
        if (src && srcFd == src->fd)
            srcMmuFlag = src->mmuFlag ? 1 : 0;

        if (src1) {
            if (src1 && src1->hnd)
                src1MmuFlag = src1Type ? 1 : 0;
            if (src1 && src1Buf == src1->virAddr)
                src1MmuFlag = 1;
            if (src1 && src1Buf == src1->phyAddr)
                src1MmuFlag = 0;
            if (src1Fd != -1)
                src1MmuFlag = src1Type ? 1 : 0;
            if (src1 && src1Fd == src1->fd)
                src1MmuFlag = src1->mmuFlag ? 1 : 0;
        }

        if (dst && dst->hnd)
            dstMmuFlag = dstType ? 1 : 0;
        if (dst && dstBuf == dst->virAddr)
            dstMmuFlag = 1;
        if (dst && dstBuf == dst->phyAddr)
            dstMmuFlag = 0;
        if (dstFd != -1)
            dstMmuFlag = dstType ? 1 : 0;
        if (dst && dstFd == dst->fd)
            dstMmuFlag = dst->mmuFlag ? 1 : 0;

#if defined(__arm64__) || defined(__aarch64__)
        NormalRgaSetSrcVirtualInfo(&rgaReg, srcFd != -1 ? srcFd : 0,
                                   (unsigned long)srcBuf,
                                   (unsigned long)srcBuf + srcVirW * srcVirH,
                                   srcVirW, srcVirH,
                                   RkRgaGetRgaFormat(relSrcRect.format),0);
        /* src1 */
        if (src1)
            NormalRgaSetPatVirtualInfo(&rgaReg, src1Fd != -1 ? src1Fd : 0,
                                       (unsigned long)src1Buf,
                                       (unsigned long)src1Buf + src1VirW * src1VirH,
                                       src1VirW, src1VirH, &clip,
                                       RkRgaGetRgaFormat(relSrc1Rect.format),0);
        /*dst*/
        NormalRgaSetDstVirtualInfo(&rgaReg, dstFd != -1 ? dstFd : 0,
                                   (unsigned long)dstBuf,
                                   (unsigned long)dstBuf + dstVirW * dstVirH,
                                   dstVirW, dstVirH, &clip,
                                   RkRgaGetRgaFormat(relDstRect.format),0);

#else
        NormalRgaSetSrcVirtualInfo(&rgaReg, srcFd != -1 ? srcFd : 0,
                                   (unsigned int)srcBuf,
                                   (unsigned int)srcBuf + srcVirW * srcVirH,
                                   srcVirW, srcVirH,
                                   RkRgaGetRgaFormat(relSrcRect.format),0);
        /* src1 */
        if (src1)
            NormalRgaSetPatVirtualInfo(&rgaReg, src1Fd != -1 ? src1Fd : 0,
                                       (unsigned int)src1Buf,
                                       (unsigned int)src1Buf + src1VirW * src1VirH,
                                       src1VirW, src1VirH, &clip,
                                       RkRgaGetRgaFormat(relSrc1Rect.format),0);
        /*dst*/
        NormalRgaSetDstVirtualInfo(&rgaReg, dstFd != -1 ? dstFd : 0,
                                   (unsigned int)dstBuf,
                                   (unsigned int)dstBuf + dstVirW * dstVirH,
                                   dstVirW, dstVirH, &clip,
                                   RkRgaGetRgaFormat(relDstRect.format),0);

#endif
    }

    /* set effective area of src and dst. */
    NormalRgaSetSrcActiveInfo(&rgaReg, srcActW, srcActH, srcXPos, srcYPos);
    NormalRgaSetDstActiveInfo(&rgaReg, dstActW, dstActH, dstXPos, dstYPos);
    if (src1)
        NormalRgaSetPatActiveInfo(&rgaReg, src1ActW, src1ActH, src1XPos, src1YPos);

    if (dst->color_space_mode & full_csc_mask) {
        NormalRgaFullColorSpaceConvert(&rgaReg, dst->color_space_mode);
    } else {
        if (src1) {
            /* special config for yuv + rgb => rgb */
            /* src0 y2r, src1 bupass, dst bupass */
            if (NormalRgaIsYuvFormat(RkRgaGetRgaFormat(relSrcRect.format)) &&
                NormalRgaIsRgbFormat(RkRgaGetRgaFormat(relSrc1Rect.format)) &&
                NormalRgaIsRgbFormat(RkRgaGetRgaFormat(relDstRect.format)))
                yuvToRgbMode |= 0x1 << 0;

            /* special config for yuv + rgba => yuv on src1 */
            /* src0 y2r, src1 bupass, dst y2r */
            if (NormalRgaIsYuvFormat(RkRgaGetRgaFormat(relSrcRect.format)) &&
                NormalRgaIsRgbFormat(RkRgaGetRgaFormat(relSrc1Rect.format)) &&
                NormalRgaIsYuvFormat(RkRgaGetRgaFormat(relDstRect.format))) {
                yuvToRgbMode |= 0x1 << 0;        //src0
                yuvToRgbMode |= 0x2 << 2;        //dst
            }

            /* special config for rgb + rgb => yuv on dst */
            /* src0 bupass, src1 bupass, dst y2r */
            if (NormalRgaIsRgbFormat(RkRgaGetRgaFormat(relSrcRect.format)) &&
                NormalRgaIsRgbFormat(RkRgaGetRgaFormat(relSrc1Rect.format)) &&
                NormalRgaIsYuvFormat(RkRgaGetRgaFormat(relDstRect.format)))
                yuvToRgbMode |= 0x2 << 2;
        } else {
            /* special config for yuv to rgb */
            if (NormalRgaIsYuvFormat(RkRgaGetRgaFormat(relSrcRect.format)) &&
                NormalRgaIsRgbFormat(RkRgaGetRgaFormat(relDstRect.format)))
                yuvToRgbMode |= 0x1 << 0;

            /* special config for rgb to yuv */
            if (NormalRgaIsRgbFormat(RkRgaGetRgaFormat(relSrcRect.format)) &&
                NormalRgaIsYuvFormat(RkRgaGetRgaFormat(relDstRect.format)))
                yuvToRgbMode |= 0x2 << 2;
        }

        if(dst->color_space_mode > 0)
            yuvToRgbMode = dst->color_space_mode;
    }

    /* mode
     * scaleMode:set different algorithm to scale.
     * rotateMode:rotation mode
     * Orientation:rotation orientation
     * ditherEn:enable or not.
     * yuvToRgbMode:yuv to rgb, rgb to yuv , or others
     * */
    NormalRgaSetBitbltMode(&rgaReg, scaleMode, rotateMode, orientation,
                           ditherEn, 0, yuvToRgbMode);

    NormalRgaNNQuantizeMode(&rgaReg, dst);

    NormalRgaDitherMode(&rgaReg, dst, relDstRect.format);

    if (srcMmuFlag || dstMmuFlag) {
        NormalRgaMmuInfo(&rgaReg, 1, 0, 0, 0, 0, 2);
        NormalRgaMmuFlag(&rgaReg, srcMmuFlag, dstMmuFlag);
    }
    if (src1) {
        if (src1MmuFlag) {
            rgaReg.mmu_info.mmu_flag |= (0x1 << 11);
            rgaReg.mmu_info.mmu_flag |= (0x1 << 9);
        }
        /*enable src0 + src1 => dst*/
        rgaReg.bsfilter_flag = 1;
    }

    /* ROP */
    /* This special Interface can do some basic logical operations */
    if(src->rop_code > 0)
    {
        rgaReg.rop_code = src->rop_code;
        rgaReg.alpha_rop_flag = 0x3;
        rgaReg.alpha_rop_mode = 0x1;
    }

    /*color key*/
    /* if need this funtion, maybe should patch the rga driver. */
    if(src->colorkey_en == 1) {
        rgaReg.alpha_rop_flag |= (1 << 9);  //real color mode
        switch (src->colorkey_mode) {
            case 0 :
                NormalRgaSetSrcTransModeInfo(&rgaReg, 0, 1, 1, 1, 1, src->colorkey_min, src->colorkey_max, 1);
                break;
            case 1 :
                NormalRgaSetSrcTransModeInfo(&rgaReg, 1, 1, 1, 1, 1, src->colorkey_min, src->colorkey_max, 1);
                break;
        }
    }

    /* mosaic */
    memcpy(&rgaReg.mosaic_info, &src->mosaic_info, sizeof(struct rga_mosaic_info));

    /* OSD */
    memcpy(&rgaReg.osd_info, &src->osd_info, sizeof(struct rga_osd_info));

    /* pre_intr */
    memcpy(&rgaReg.pre_intr_info, &src->pre_intr, sizeof(src->pre_intr));

#if __DEBUG
    NormalRgaLogOutRgaReq(rgaReg);
#endif

    if(src->sync_mode == RGA_BLIT_ASYNC || dst->sync_mode == RGA_BLIT_ASYNC) {
        sync_mode = RGA_BLIT_ASYNC;
    }

    /* rga3 rd_mode */
    /* If rd_mode is not configured, raster mode is executed by default. */
	rgaReg.src.rd_mode = src->rd_mode ? src->rd_mode : raster_mode;
	rgaReg.dst.rd_mode = dst->rd_mode ? dst->rd_mode : raster_mode;
	if (src1)
		rgaReg.pat.rd_mode = src1->rd_mode ? src1->rd_mode : raster_mode;

	rgaReg.in_fence_fd = dst->in_fence_fd;
	rgaReg.core = dst->core;
	rgaReg.priority = dst->priority;

    do {
        ret = ioctl(ctx->rgaFd, sync_mode, &rgaReg);
    } while (ret == -1 && (errno == EINTR || errno == 512));   /* ERESTARTSYS is 512. */
    if(ret) {
        ALOGE(" %s(%d) RGA_BLIT fail: %s",__FUNCTION__, __LINE__,strerror(errno));
        return -errno;
    }

	dst->out_fence_fd = rgaReg.out_fence_fd;

    return 0;
}

int RgaFlush() {
    struct rgaContext *ctx = rgaCtx;

    //init context
    if (!ctx) {
        ALOGE("Try to use uninit rgaCtx=%p",ctx);
        return -ENODEV;
    }

    if(ioctl(ctx->rgaFd, RGA_FLUSH, NULL)) {
        ALOGE(" %s(%d) RGA_FLUSH fail: %s",__FUNCTION__, __LINE__,strerror(errno));
        return -errno;
    }
    return 0;
}

int RgaCollorFill(rga_info_t *dst) {
    //check rects
    //check buffer_handle_t with rects
    struct rgaContext *ctx = rgaCtx;
    int dstVirW,dstVirH,dstActW,dstActH,dstXPos,dstYPos;
    int scaleMode,ditherEn;
    int dstType,dstMmuFlag;
    int dstFd = -1;
    int ret = 0;
    unsigned int color = 0x00000000;
    rga_rect_t relDstRect,tmpDstRect;
    struct rga_req rgaReg;
    COLOR_FILL fillColor ;
    void *dstBuf = NULL;
    RECT clip;

    int sync_mode = RGA_BLIT_SYNC;

    if (!ctx) {
        ALOGE("Try to use uninit rgaCtx=%p",ctx);
        return -ENODEV;
    }

    memset(&rgaReg, 0, sizeof(struct rga_req));

    dstType = dstMmuFlag = 0;

    if (!dst) {
        ALOGE("dst = %p", dst);
        return -EINVAL;
    }

    if (dst) {
        color = dst->color;
        memcpy(&relDstRect, &dst->rect, sizeof(rga_rect_t));
    }

    dstFd = -1;

    if (relDstRect.hstride == 0)
        relDstRect.hstride = relDstRect.height;

    if (dst && dstFd < 0) {
        if (dst->handle > 0) {
            dstFd = dst->handle;
            /* This will mark the use of handle */
            rgaReg.handle_flag |= 1;
        } else {
            dstFd = dst->fd;
        }
    }

    if (dst && dst->phyAddr)
        dstBuf = dst->phyAddr;
    else if (dst && dst->virAddr)
        dstBuf = dst->virAddr;

    if (dst && dstFd == -1 && !dstBuf) {
        ALOGE("%d:dst has not fd and address for render", __LINE__);
        return ret;
    }

    if (dst && dstFd == 0 && !dstBuf) {
        ALOGE("dstFd is zero, now driver not support");
        return -EINVAL;
    }

    relDstRect.format = RkRgaCompatibleFormat(relDstRect.format);

    if (dstFd == 0)
        dstFd = -1;

    if (relDstRect.hstride == 0)
        relDstRect.hstride = relDstRect.height;

    dstVirW = relDstRect.wstride;
    dstVirH = relDstRect.hstride;
    dstXPos = relDstRect.xoffset;
    dstYPos = relDstRect.yoffset;
    dstActW = relDstRect.width;
    dstActH = relDstRect.height;

    clip.xmin = 0;
    clip.xmax = dstActW - 1;
    clip.ymin = 0;
    clip.ymax = dstActH - 1;

    if (ctx->mVersion <= 1.003) {
#if defined(__arm64__) || defined(__aarch64__)
        /*dst*/
        NormalRgaSetDstVirtualInfo(&rgaReg, (unsigned long)dstBuf,
                                   (unsigned long)dstBuf + dstVirW * dstVirH,
                                   (unsigned long)dstBuf + dstVirW * dstVirH * 5/4,
                                   dstVirW, dstVirH, &clip,
                                   RkRgaGetRgaFormat(relDstRect.format),0);
#else
        /*dst*/
        NormalRgaSetDstVirtualInfo(&rgaReg, (unsigned int)dstBuf,
                                   (unsigned int)dstBuf + dstVirW * dstVirH,
                                   (unsigned int)dstBuf + dstVirW * dstVirH * 5/4,
                                   dstVirW, dstVirH, &clip,
                                   RkRgaGetRgaFormat(relDstRect.format),0);
#endif
    } else if (ctx->mVersion < 1.6 ) {
        /*dst*/
        if (dstFd != -1) {
            dstMmuFlag = dstType ? 1 : 0;
            if (dst && dstFd == dst->fd)
                dstMmuFlag = dst->mmuFlag ? 1 : 0;
            NormalRgaSetDstVirtualInfo(&rgaReg, 0, 0, 0, dstVirW, dstVirH, &clip,
                                       RkRgaGetRgaFormat(relDstRect.format),0);
            /*src dst fd*/
            NormalRgaSetFdsOffsets(&rgaReg, 0, dstFd, 0, 0);
        } else {
            if (dst && dst->hnd)
                dstMmuFlag = dstType ? 1 : 0;
            if (dst && dstBuf == dst->virAddr)
                dstMmuFlag = 1;
            if (dst && dstBuf == dst->phyAddr)
                dstMmuFlag = 0;
#if defined(__arm64__) || defined(__aarch64__)
            NormalRgaSetDstVirtualInfo(&rgaReg, (unsigned long)dstBuf,
                                       (unsigned long)dstBuf + dstVirW * dstVirH,
                                       (unsigned long)dstBuf + dstVirW * dstVirH * 5/4,
                                       dstVirW, dstVirH, &clip,
                                       RkRgaGetRgaFormat(relDstRect.format),0);
#else
            NormalRgaSetDstVirtualInfo(&rgaReg, (unsigned int)dstBuf,
                                       (unsigned int)dstBuf + dstVirW * dstVirH,
                                       (unsigned int)dstBuf + dstVirW * dstVirH * 5/4,
                                       dstVirW, dstVirH, &clip,
                                       RkRgaGetRgaFormat(relDstRect.format),0);
#endif
        }
    } else {
        if (dst && dst->hnd)
            dstMmuFlag = dstType ? 1 : 0;
        if (dst && dstBuf == dst->virAddr)
            dstMmuFlag = 1;
        if (dst && dstBuf == dst->phyAddr)
            dstMmuFlag = 0;
        if (dstFd != -1)
            dstMmuFlag = dstType ? 1 : 0;
        if (dst && dstFd == dst->fd)
            dstMmuFlag = dst->mmuFlag ? 1 : 0;
#if defined(__arm64__) || defined(__aarch64__)
        /*dst*/
        NormalRgaSetDstVirtualInfo(&rgaReg, dstFd != -1 ? dstFd : 0,
                                   (unsigned long)dstBuf,
                                   (unsigned long)dstBuf + dstVirW * dstVirH,
                                   dstVirW, dstVirH, &clip,
                                   RkRgaGetRgaFormat(relDstRect.format),0);
#else
        /*dst*/
        NormalRgaSetDstVirtualInfo(&rgaReg, dstFd != -1 ? dstFd : 0,
                                   (unsigned int)dstBuf,
                                   (unsigned int)dstBuf + dstVirW * dstVirH,
                                   dstVirW, dstVirH, &clip,
                                   RkRgaGetRgaFormat(relDstRect.format),0);
#endif
    }

    if (NormalRgaIsYuvFormat(RkRgaGetRgaFormat(relDstRect.format))) {
        rgaReg.yuv2rgb_mode |= 0x2 << 2;
    }

    if(dst->color_space_mode > 0)
        rgaReg.yuv2rgb_mode = dst->color_space_mode;

    NormalRgaSetDstActiveInfo(&rgaReg, dstActW, dstActH, dstXPos, dstYPos);

    memset(&fillColor, 0x0, sizeof(COLOR_FILL));

    /*mode*/
    NormalRgaSetColorFillMode(&rgaReg, &fillColor, 0, 0, color, 0, 0, 0, 0, 0);

    if (dstMmuFlag) {
        NormalRgaMmuInfo(&rgaReg, 1, 0, 0, 0, 0, 2);
        NormalRgaMmuFlag(&rgaReg, dstMmuFlag, dstMmuFlag);
    }

#if __DEBUG
    NormalRgaLogOutRgaReq(rgaReg);
#endif

    if(dst->sync_mode == RGA_BLIT_ASYNC) {
        sync_mode = dst->sync_mode;
    }

    /* rga3 rd_mode */
    /* If rd_mode is not configured, raster mode is executed by default. */
	rgaReg.dst.rd_mode = dst->rd_mode ? dst->rd_mode : raster_mode;

	rgaReg.in_fence_fd = dst->in_fence_fd;
	rgaReg.core = dst->core;
	rgaReg.priority = dst->priority;

    do {
        ret = ioctl(ctx->rgaFd, sync_mode, &rgaReg);
    } while (ret == -1 && (errno == EINTR || errno == 512));   /* ERESTARTSYS is 512. */
    if(ret) {
        ALOGE(" %s(%d) RGA_COLORFILL fail: %s",__FUNCTION__, __LINE__,strerror(errno));
        return -errno;
    }

    return 0;
}

int RgaCollorPalette(rga_info_t *src, rga_info_t *dst, rga_info_t *lut) {

    struct rgaContext *ctx = rgaCtx;
    struct rga_req  Rga_Request;
    struct rga_req  Rga_Request2;
    int srcVirW ,srcVirH ,srcActW ,srcActH ,srcXPos ,srcYPos;
    int dstVirW ,dstVirH ,dstActW ,dstActH ,dstXPos ,dstYPos;
    int lutVirW ,lutVirH ,lutActW ,lutActH ,lutXPos ,lutYPos;
    int srcType ,dstType ,lutType ,srcMmuFlag ,dstMmuFlag, lutMmuFlag;
    int dstFd = -1;
    int srcFd = -1;
    int lutFd = -1;
    int ret = 0;
    rga_rect_t relSrcRect,tmpSrcRect,relDstRect,tmpDstRect, relLutRect, tmpLutRect;
    struct rga_req rgaReg,tmprgaReg;
    void *srcBuf = NULL;
    void *dstBuf = NULL;
    void *lutBuf = NULL;
    RECT clip;

    //init context
    if (!ctx) {
        ALOGE("Try to use uninit rgaCtx=%p",ctx);
        return -ENODEV;
    }

    //init
    memset(&rgaReg, 0, sizeof(struct rga_req));

    srcType = dstType = lutType = srcMmuFlag = dstMmuFlag = lutMmuFlag = 0;

    if (!src && !dst) {
        ALOGE("src = %p, dst = %p, lut = %p", src, dst, lut);
        return -EINVAL;
    }

     /* get effective area from src、dst and lut, if the area is empty, choose to get parameter from handle. */
    if (src)
        memcpy(&relSrcRect, &src->rect, sizeof(rga_rect_t));
    if (dst)
        memcpy(&relDstRect, &dst->rect, sizeof(rga_rect_t));
    if (lut)
        memcpy(&relLutRect, &lut->rect, sizeof(rga_rect_t));

    srcFd = dstFd = lutFd = -1;

    if (lut) {
        if (src->handle > 0 && dst->handle > 0 && lut->handle > 0) {
            if (src->handle <= 0 || dst->handle <= 0 || lut->handle <= 0) {
                ALOGE("librga only supports the use of handles only or no handles, [src,lut,dst] = [%d, %d, %d]",
                      src->handle, lut->handle, dst->handle);
                return -EINVAL;
            }

            /* This will mark the use of handle */
            rgaReg.handle_flag |= 1;
        }
    } else if (src->handle > 0 && dst->handle > 0) {
        if (src->handle <= 0 || dst->handle <= 0) {
            ALOGE("librga only supports the use of handles only or no handles, [src,dst] = [%d, %d]",
                  src->handle, dst->handle);
            return -EINVAL;
        }

        /* This will mark the use of handle */
        rgaReg.handle_flag |= 1;
    }

    /*********** get src addr *************/
    if (src && src->handle) {
        /* In order to minimize changes, the handle here will reuse the variable of Fd. */
        srcFd = src->handle;
    } else if (src && src->phyAddr) {
        srcBuf = src->phyAddr;
    } else if (src && src->fd > 0) {
        srcFd = src->fd;
        src->mmuFlag = 1;
    } else if (src && src->virAddr) {
        srcBuf = src->virAddr;
        src->mmuFlag = 1;
    }

    if (srcFd == -1 && !srcBuf) {
        ALOGE("%d:src has not fd and address for render", __LINE__);
        return ret;
    }
    if (srcFd == 0 && !srcBuf) {
        ALOGE("srcFd is zero, now driver not support");
        return -EINVAL;
    }
    /* Old rga driver cannot support fd as zero. */
    if (srcFd == 0)
        srcFd = -1;

    /*********** get dst addr *************/
    if (dst && dst->handle) {
        /* In order to minimize changes, the handle here will reuse the variable of Fd. */
        dstFd = dst->handle;
    } else if (dst && dst->phyAddr) {
        dstBuf = dst->phyAddr;
    } else if (dst && dst->fd > 0) {
        dstFd = dst->fd;
        dst->mmuFlag = 1;
    } else if (dst && dst->virAddr) {
        dstBuf = dst->virAddr;
        dst->mmuFlag = 1;
    }

    if (dstFd == -1 && !dstBuf) {
        ALOGE("%d:dst has not fd and address for render", __LINE__);
        return ret;
    }
    if (dstFd == 0 && !dstBuf) {
        ALOGE("dstFd is zero, now driver not support");
        return -EINVAL;
    }
    /* Old rga driver cannot support fd as zero. */
    if (dstFd == 0)
        dstFd = -1;

    /*********** get lut addr *************/
    if (lut && lut->handle) {
        /* In order to minimize changes, the handle here will reuse the variable of Fd. */
        lutFd = lut->handle;
    } else if (lut && lut->phyAddr) {
        lutBuf = lut->phyAddr;
    } else if (lut && lut->fd > 0) {
        lutFd = lut->fd;
        lut->mmuFlag = 1;
    } else if (lut && lut->virAddr) {
        lutBuf = lut->virAddr;
        lut->mmuFlag = 1;
    }
    /* Old rga driver cannot support fd as zero. */
    if (lutFd == 0)
        lutFd = -1;

    relSrcRect.format = RkRgaCompatibleFormat(relSrcRect.format);
    relDstRect.format = RkRgaCompatibleFormat(relDstRect.format);
    relLutRect.format = RkRgaCompatibleFormat(relLutRect.format);

#ifdef RK3126C
    if ( (relSrcRect.width == relDstRect.width) && (relSrcRect.height == relDstRect.height ) &&
         (relSrcRect.width + 2*relSrcRect.xoffset == relSrcRect.wstride) &&
         (relSrcRect.height + 2*relSrcRect.yoffset == relSrcRect.hstride) &&
         (relSrcRect.format == HAL_PIXEL_FORMAT_YCrCb_NV12) && (relSrcRect.xoffset > 0 && relSrcRect.yoffset > 0)
       ) {
        relSrcRect.width += 4;
        //relSrcRect.height += 4;
        relSrcRect.xoffset = (relSrcRect.wstride - relSrcRect.width) / 2;
    }
#endif

    /* discripe a picture need high stride.If high stride not to be set, need use height as high stride. */
    if (relSrcRect.hstride == 0)
        relSrcRect.hstride = relSrcRect.height;

    if (relDstRect.hstride == 0)
        relDstRect.hstride = relDstRect.height;

    /* do some check, check the area of src and dst whether is effective. */
    if (src) {
        ret = checkRectForRga(relSrcRect);
        if (ret) {
            ALOGE("[%s,%d]Error srcRect ", __func__, __LINE__);
            return ret;
        }
    }

    if (dst) {
        ret = checkRectForRga(relDstRect);
        if (ret) {
            ALOGE("[%s,%d]Error dstRect ", __func__, __LINE__);
            return ret;
        }
    }

    srcVirW = relSrcRect.wstride;
    srcVirH = relSrcRect.hstride;
    srcXPos = relSrcRect.xoffset;
    srcYPos = relSrcRect.yoffset;
    srcActW = relSrcRect.width;
    srcActH = relSrcRect.height;

    dstVirW = relDstRect.wstride;
    dstVirH = relDstRect.hstride;
    dstXPos = relDstRect.xoffset;
    dstYPos = relDstRect.yoffset;
    dstActW = relDstRect.width;
    dstActH = relDstRect.height;

    lutVirW = relLutRect.wstride;
    lutVirH = relLutRect.hstride;
    lutXPos = relLutRect.xoffset;
    lutYPos = relLutRect.yoffset;
    lutActW = relLutRect.width;
    lutActH = relLutRect.height;

    /* if pictual out of range should be cliped. */
    clip.xmin = 0;
    clip.xmax = dstVirW - 1;
    clip.ymin = 0;
    clip.ymax = dstVirH - 1;

    /* only to configure the parameter by driver version, because rga driver has too many version. */
    if (ctx->mVersion <= (float)1.003) {
        srcMmuFlag = dstMmuFlag = lutMmuFlag = 1;

#if defined(__arm64__) || defined(__aarch64__)
        NormalRgaSetSrcVirtualInfo(&rgaReg, (unsigned long)srcBuf,
                                   (unsigned long)srcBuf + srcVirW * srcVirH,
                                   (unsigned long)srcBuf + srcVirW * srcVirH * 5/4,
                                   srcVirW, srcVirH,
                                   RkRgaGetRgaFormat(relSrcRect.format),0);
        /*dst*/
        NormalRgaSetDstVirtualInfo(&rgaReg, (unsigned long)dstBuf,
                                   (unsigned long)dstBuf + dstVirW * dstVirH,
                                   (unsigned long)dstBuf + dstVirW * dstVirH * 5/4,
                                   dstVirW, dstVirH, &clip,
                                   RkRgaGetRgaFormat(relDstRect.format),0);
        /*lut*/
        NormalRgaSetPatVirtualInfo(&rgaReg, (unsigned long)lutBuf,
                                   (unsigned long)lutBuf + lutVirW * lutVirH,
                                   (unsigned long)lutBuf + lutVirW * lutVirH * 5/4,
                                   lutVirW, lutVirH, &clip,
                                   RkRgaGetRgaFormat(relLutRect.format),0);
#else
        NormalRgaSetSrcVirtualInfo(&rgaReg, (unsigned long)srcBuf,
                                   (unsigned int)srcBuf + srcVirW * srcVirH,
                                   (unsigned int)srcBuf + srcVirW * srcVirH * 5/4,
                                   srcVirW, srcVirH,
                                   RkRgaGetRgaFormat(relSrcRect.format),0);
        /*dst*/
        NormalRgaSetDstVirtualInfo(&rgaReg, (unsigned long)dstBuf,
                                   (unsigned int)dstBuf + dstVirW * dstVirH,
                                   (unsigned int)dstBuf + dstVirW * dstVirH * 5/4,
                                   dstVirW, dstVirH, &clip,
                                   RkRgaGetRgaFormat(relDstRect.format),0);
        /*lut*/
        NormalRgaSetPatVirtualInfo(&rgaReg, (unsigned long)lutBuf,
                                   (unsigned int)lutBuf + lutVirW * lutVirH,
                                   (unsigned int)lutBuf + lutVirW * lutVirH * 5/4,
                                   lutVirW, lutVirH, &clip,
                                   RkRgaGetRgaFormat(relLutRect.format),0);

#endif
        /* the version 1.005 is different to assign fd from version 2.0 and above */
    } else if (ctx->mVersion < (float)1.6) {
        /*Src*/
        if (srcFd != -1) {
            srcMmuFlag = srcType ? 1 : 0;
            if (src && srcFd == src->fd)
                srcMmuFlag = src->mmuFlag ? 1 : 0;
            NormalRgaSetSrcVirtualInfo(&rgaReg, 0, 0, 0, srcVirW, srcVirH,
                                       RkRgaGetRgaFormat(relSrcRect.format),0);
            NormalRgaSetFdsOffsets(&rgaReg, srcFd, 0, 0, 0);
        } else {
            if (src && src->hnd)
                srcMmuFlag = srcType ? 1 : 0;
            if (src && srcBuf == src->virAddr)
                srcMmuFlag = 1;
            if (src && srcBuf == src->phyAddr)
                srcMmuFlag = 0;
#if defined(__arm64__) || defined(__aarch64__)
            NormalRgaSetSrcVirtualInfo(&rgaReg, (unsigned long)srcBuf,
                                       (unsigned long)srcBuf + srcVirW * srcVirH,
                                       (unsigned long)srcBuf + srcVirW * srcVirH * 5/4,
                                       srcVirW, srcVirH,
                                       RkRgaGetRgaFormat(relSrcRect.format),0);
#else
            NormalRgaSetSrcVirtualInfo(&rgaReg, (unsigned int)srcBuf,
                                       (unsigned int)srcBuf + srcVirW * srcVirH,
                                       (unsigned int)srcBuf + srcVirW * srcVirH * 5/4,
                                       srcVirW, srcVirH,
                                       RkRgaGetRgaFormat(relSrcRect.format),0);
#endif
        }
        /*dst*/
        if (dstFd != -1) {
            dstMmuFlag = dstType ? 1 : 0;
            if (dst && dstFd == dst->fd)
                dstMmuFlag = dst->mmuFlag ? 1 : 0;
            NormalRgaSetDstVirtualInfo(&rgaReg, 0, 0, 0, dstVirW, dstVirH, &clip,
                                       RkRgaGetRgaFormat(relDstRect.format),0);
            /*src dst fd*/
            NormalRgaSetFdsOffsets(&rgaReg, 0, dstFd, 0, 0);
        } else {
            if (dst && dst->hnd)
                dstMmuFlag = dstType ? 1 : 0;
            if (dst && dstBuf == dst->virAddr)
                dstMmuFlag = 1;
            if (dst && dstBuf == dst->phyAddr)
                dstMmuFlag = 0;
#if defined(__arm64__) || defined(__aarch64__)
            NormalRgaSetDstVirtualInfo(&rgaReg, (unsigned long)dstBuf,
                                       (unsigned long)dstBuf + dstVirW * dstVirH,
                                       (unsigned long)dstBuf + dstVirW * dstVirH * 5/4,
                                       dstVirW, dstVirH, &clip,
                                       RkRgaGetRgaFormat(relDstRect.format),0);
#else
            NormalRgaSetDstVirtualInfo(&rgaReg, (unsigned int)dstBuf,
                                       (unsigned int)dstBuf + dstVirW * dstVirH,
                                       (unsigned int)dstBuf + dstVirW * dstVirH * 5/4,
                                       dstVirW, dstVirH, &clip,
                                       RkRgaGetRgaFormat(relDstRect.format),0);
#endif
        }
        /*lut*/
        if (lutFd != -1) {
            lutMmuFlag = lutType ? 1 : 0;
            if (lut && lutFd == lut->fd)
                lutMmuFlag = lut->mmuFlag ? 1 : 0;
            NormalRgaSetPatVirtualInfo(&rgaReg, 0, 0, 0, lutVirW, lutVirH, &clip,
                                       RkRgaGetRgaFormat(relLutRect.format),0);
            /*lut fd*/
            NormalRgaSetFdsOffsets(&rgaReg, 0, lutFd, 0, 0);
        } else {
            if (lut && lut->hnd)
                lutMmuFlag = lutType ? 1 : 0;
            if (lut && lutBuf == lut->virAddr)
                lutMmuFlag = 1;
            if (lut && lutBuf == lut->phyAddr)
                lutMmuFlag = 0;
#if defined(__arm64__) || defined(__aarch64__)
            NormalRgaSetPatVirtualInfo(&rgaReg, (unsigned long)lutBuf,
                                       (unsigned long)lutBuf + lutVirW * lutVirH,
                                       (unsigned long)lutBuf + lutVirW * lutVirH * 5/4,
                                       lutVirW, lutVirH, &clip,
                                       RkRgaGetRgaFormat(relLutRect.format),0);
#else
            NormalRgaSetPatVirtualInfo(&rgaReg, (unsigned int)lutBuf,
                                       (unsigned int)lutBuf + lutVirW * lutVirH,
                                       (unsigned int)lutBuf + lutVirW * lutVirH * 5/4,
                                       lutVirW, lutVirH, &clip,
                                       RkRgaGetRgaFormat(relLutRect.format),0);
#endif
        }
    } else {
        if (src && src->hnd)
            srcMmuFlag = srcType ? 1 : 0;
        if (src && srcBuf == src->virAddr)
            srcMmuFlag = 1;
        if (src && srcBuf == src->phyAddr)
            srcMmuFlag = 0;
        if (srcFd != -1)
            srcMmuFlag = srcType ? 1 : 0;
        if (src && srcFd == src->fd)
            srcMmuFlag = src->mmuFlag ? 1 : 0;

        if (dst && dst->hnd)
            dstMmuFlag = dstType ? 1 : 0;
        if (dst && dstBuf == dst->virAddr)
            dstMmuFlag = 1;
        if (dst && dstBuf == dst->phyAddr)
            dstMmuFlag = 0;
        if (dstFd != -1)
            dstMmuFlag = dstType ? 1 : 0;
        if (dst && dstFd == dst->fd)
            dstMmuFlag = dst->mmuFlag ? 1 : 0;

        if (lut && lut->hnd)
            lutMmuFlag = lutType ? 1 : 0;
        if (lut && lutBuf == lut->virAddr)
            lutMmuFlag = 1;
        if (lut && lutBuf == lut->phyAddr)
            lutMmuFlag = 0;
        if (lutFd != -1)
            lutMmuFlag = lutType ? 1 : 0;
        if (lut && lutFd == lut->fd)
            lutMmuFlag = lut->mmuFlag ? 1 : 0;

#if defined(__arm64__) || defined(__aarch64__)
        NormalRgaSetSrcVirtualInfo(&rgaReg, srcFd != -1 ? srcFd : 0,
                                   (unsigned long)srcBuf,
                                   (unsigned long)srcBuf + srcVirW * srcVirH,
                                   srcVirW, srcVirH,
                                   RkRgaGetRgaFormat(relSrcRect.format),0);
        /*dst*/
        NormalRgaSetDstVirtualInfo(&rgaReg, dstFd != -1 ? dstFd : 0,
                                   (unsigned long)dstBuf,
                                   (unsigned long)dstBuf + dstVirW * dstVirH,
                                   dstVirW, dstVirH, &clip,
                                   RkRgaGetRgaFormat(relDstRect.format),0);

        /*lut*/
        NormalRgaSetPatVirtualInfo(&rgaReg, lutFd != -1 ? lutFd : 0,
                                   (unsigned long)lutBuf,
                                   (unsigned long)lutBuf + lutVirW * lutVirH,
                                   lutVirW, lutVirH, &clip,
                                   RkRgaGetRgaFormat(relLutRect.format),0);
#else
        NormalRgaSetSrcVirtualInfo(&rgaReg, srcFd != -1 ? srcFd : 0,
                                   (unsigned int)srcBuf,
                                   (unsigned int)srcBuf + srcVirW * srcVirH,
                                   srcVirW, srcVirH,
                                   RkRgaGetRgaFormat(relSrcRect.format),0);
        /*dst*/
        NormalRgaSetDstVirtualInfo(&rgaReg, dstFd != -1 ? dstFd : 0,
                                   (unsigned int)dstBuf,
                                   (unsigned int)dstBuf + dstVirW * dstVirH,
                                   dstVirW, dstVirH, &clip,
                                   RkRgaGetRgaFormat(relDstRect.format),0);
        /*lut*/
        NormalRgaSetPatVirtualInfo(&rgaReg, lutFd != -1 ? lutFd : 0,
                                  (unsigned int)lutBuf,
                                  (unsigned int)lutBuf + lutVirW * lutVirH,
                                  lutVirW, lutVirH, &clip,
                                  RkRgaGetRgaFormat(relLutRect.format),0);

#endif
    }

    /* set effective area of src and dst. */
    NormalRgaSetSrcActiveInfo(&rgaReg, srcActW, srcActH, srcXPos, srcYPos);
    NormalRgaSetDstActiveInfo(&rgaReg, dstActW, dstActH, dstXPos, dstYPos);
    NormalRgaSetPatActiveInfo(&rgaReg, lutActW, lutActH, lutXPos, lutYPos);

    if (srcMmuFlag || dstMmuFlag || lutMmuFlag) {
        NormalRgaMmuInfo(&rgaReg, 1, 0, 0, 0, 0, 2);
        NormalRgaMmuFlag(&rgaReg, srcMmuFlag, dstMmuFlag);
        /*set lut mmu_flag*/
        if (lutMmuFlag) {
            rgaReg.mmu_info.mmu_flag |= (0x1 << 11);
            rgaReg.mmu_info.mmu_flag |= (0x1 << 9);
        }

    }

#if __DEBUG
    NormalRgaLogOutRgaReq(rgaReg);
#endif

    switch (RkRgaGetRgaFormat(relSrcRect.format)) {
        case RK_FORMAT_BPP1 :
            rgaReg.palette_mode = 0;
            break;
        case RK_FORMAT_BPP2 :
            rgaReg.palette_mode = 1;
            break;
        case RK_FORMAT_BPP4 :
            rgaReg.palette_mode = 2;
            break;
        case RK_FORMAT_BPP8 :
            rgaReg.palette_mode = 3;
            break;
    }

    /* rga3 rd_mode */
    /* If rd_mode is not configured, raster mode is executed by default. */
	rgaReg.src.rd_mode = src->rd_mode ? src->rd_mode : raster_mode;
	rgaReg.dst.rd_mode = dst->rd_mode ? dst->rd_mode : raster_mode;
	if (lut)
		rgaReg.pat.rd_mode = lut->rd_mode ? lut->rd_mode : raster_mode;

    rgaReg.in_fence_fd = dst->in_fence_fd;
	rgaReg.core = dst->core;
	rgaReg.priority = dst->priority;

    if (!(lutFd == -1 && lutBuf == NULL)) {
        rgaReg.fading.g = 0xff;
        rgaReg.render_mode = update_palette_table_mode;

        if(ioctl(ctx->rgaFd, RGA_BLIT_SYNC, &rgaReg) != 0) {
            ALOGE("update palette table mode ioctl err");
            return -1;
        }
    }

    rgaReg.render_mode = color_palette_mode;
    rgaReg.endian_mode = 1;

    do {
        ret = ioctl(ctx->rgaFd, RGA_BLIT_SYNC, &rgaReg);
    } while (ret == -1 && (errno == EINTR || errno == 512));   /* ERESTARTSYS is 512. */
    if(ret) {
        ALOGE(" %s(%d) RGA_COLOR_PALETTE fail: %s",__FUNCTION__, __LINE__,strerror(errno));
        return -errno;
    }

    return 0;
}

int NormalRgaScale() {
    return 1;
}

int NormalRgaRoate() {
    return 1;
}

int NormalRgaRoateScale() {
    return 1;
}
