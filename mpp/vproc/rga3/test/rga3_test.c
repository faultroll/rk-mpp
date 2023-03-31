
#include <time.h>

#include "NormalRga.h"

#include "rk_type.h"
#include "mpp_err.h"
#include "mpp_frame.h"

int main() {
    printf("rga3 test unit\n");
    void *ctx;
    RgaInit(&ctx);

    MPP_RET ret = MPP_NOK;
    MppBuffer src_buf = NULL;
    MppBuffer dst_buf = NULL;
    MppFrame src_frm = NULL;
    MppFrame dst_frm = NULL;
    FILE *fin = NULL;
    FILE *fout = NULL;

    void *ptr;
    RK_U32 src_x = 0;
    RK_U32 src_y = 0;
    RK_U32 src_w = 1920;
    RK_U32 src_h = 1080;
    RK_U32 src_hor = 1920;//src_w;
    RK_U32 src_ver = 1080;//src_h;
    RK_U32 dst_x = 0;
    RK_U32 dst_y = 0;
    RK_U32 dst_w = 2560;
    RK_U32 dst_h = 1440;
    RK_U32 dst_hor = 2560;//src_w;
    RK_U32 dst_ver = 1440;//dst_h;
    RK_U32 src_size = src_hor * src_ver * 3 >> 1;
    RK_U32 dst_size = dst_hor * dst_ver * 3 >> 1;

    fin = fopen("input.dat", "rb");
    if (!fin) {
        printf("open input file failed\n");
        goto END;
    }

    fout = fopen("output.dat", "w+b");
    if (!fout) {
        printf("open output file failed\n");
        goto END;
    }

    ret = mpp_buffer_get(NULL, &src_buf, src_size);
    if (ret) {
        printf("failed to get src buffer %d with size %d\n", ret, src_size);
        goto END;
    }

    ret = mpp_buffer_get(NULL, &dst_buf, dst_size);
    if (ret) {
        printf("failed to get dst buffer %d with size %d\n", ret, dst_size);
        goto END;
    }

    ret = mpp_frame_init(&src_frm);
    if (ret) {
        printf("failed to init src frame\n");
        goto END;
    }

    ret = mpp_frame_init(&dst_frm);
    if (ret) {
        printf("failed to init dst frame\n");
        goto END;
    }

    mpp_frame_set_buffer(src_frm, src_buf);
    mpp_frame_set_offset_x(src_frm, src_x);
    mpp_frame_set_offset_y(src_frm, src_y);
    mpp_frame_set_width(src_frm, src_w);
    mpp_frame_set_height(src_frm, src_h);
    mpp_frame_set_hor_stride(src_frm, src_hor);
    mpp_frame_set_ver_stride(src_frm, src_ver);
    mpp_frame_set_fmt(src_frm, MPP_FMT_YUV420SP);

    mpp_frame_set_buffer(dst_frm, dst_buf);
    mpp_frame_set_offset_x(dst_frm, dst_x);
    mpp_frame_set_offset_y(dst_frm, dst_y);
    mpp_frame_set_width(dst_frm, dst_w);
    mpp_frame_set_height(dst_frm, dst_h);
    mpp_frame_set_hor_stride(dst_frm, dst_hor);
    mpp_frame_set_ver_stride(dst_frm, dst_ver);
    mpp_frame_set_fmt(dst_frm, MPP_FMT_YUV420SP);

    do {
        /********** rga_info_t Init **********/
        rga_info_t src;
        rga_info_t dst;

        memset(&src, 0, sizeof(rga_info_t));
        src.fd = mpp_buffer_get_fd(src_buf);
        src.mmuFlag = 1;
        src.hnd = -1;
        // src.phyAddr = ;
        src.virAddr = mpp_buffer_get_ptr(src_buf);
        src.sync_mode = RGA_BLIT_SYNC;

        memset(&dst, 0, sizeof(rga_info_t));
        dst.fd = mpp_buffer_get_fd(dst_buf);
        dst.mmuFlag = 1;
        dst.hnd = -1;
        // dst.phyAddr = ;
        dst.virAddr = mpp_buffer_get_ptr(dst_buf);
        dst.sync_mode = RGA_BLIT_SYNC;

        /********** get src_Fd **********/
        /* ret = rkRga.RkRgaGetBufferFd(gbs->handle, &src.fd);
        printf("src.fd =%d\n",src.fd);
        if (ret) {
            printf("rgaGetsrcFd fail : %s,hnd=%p \n",
                   strerror(errno),(void*)(gbd->handle));
        } */
        /********** get dst_Fd **********/
        /* ret = rkRga.RkRgaGetBufferFd(gbd->handle, &dst.fd);
        printf("dst.fd =%d \n",dst.fd);
        ALOGD("dst.fd =%d\n",dst.fd);
        if (ret) {
            printf("rgaGetdstFd error : %s,hnd=%p\n",
                   strerror(errno),(void*)(gbd->handle));
        } */
        /********** if not fd, try to check phyAddr and virAddr **************/
        if(src.fd <= 0|| dst.fd <= 0)
        {
            /********** check phyAddr and virAddr ,if none to get virAddr **********/
            if (( src.phyAddr != 0 || src.virAddr != 0 ) || src.hnd != NULL ) {
                /* ret = RkRgaGetHandleMapAddress( gbs->handle, &src.virAddr );
                printf("src.virAddr =%p\n",src.virAddr);
                if(!src.virAddr) {
                    printf("err! src has not fd and address for render ,Stop!\n");
                    break;
                } */
            }

            /********** check phyAddr and virAddr ,if none to get virAddr **********/
            if (( dst.phyAddr != 0 || dst.virAddr != 0 ) || dst.hnd != NULL ) {
                /* ret = RkRgaGetHandleMapAddress( gbd->handle, &dst.virAddr );
                printf("dst.virAddr =%p\n",dst.virAddr);
                if(!dst.virAddr) {
                    printf("err! dst has not fd and address for render ,Stop!\n");
                    break;
                } */
            }
        }

        {
            /********** input buf data from file **********/
            ptr = mpp_buffer_get_ptr(src_buf);
            fread(ptr, 1, src_size, fin);
        }

        /********** set the rect_info **********/
        rga_set_rect(&src.rect,src_x,src_y,src_w,src_h,src_hor/*stride*/,src_ver,RK_FORMAT_YCbCr_420_SP);
        rga_set_rect(&dst.rect,dst_x,dst_y,dst_w,dst_h,dst_hor/*stride*/,dst_ver,RK_FORMAT_YCbCr_420_SP);

        /************ set the rga_mod ,rotation\composition\scale\copy .... **********/


        /********** call rga_Interface **********/
        struct timeval tpend1, tpend2;
        long usec1 = 0;
        gettimeofday(&tpend1, NULL);

        ret = RgaBlit(&src, &dst, NULL);
        if (ret) {
            printf("RgaBlit error : %s\n", strerror(errno));
        }

        gettimeofday(&tpend2, NULL);
        usec1 = 1000 * (tpend2.tv_sec - tpend1.tv_sec) + (tpend2.tv_usec - tpend1.tv_usec) / 1000;
        printf("cost_time=%ld ms\n", usec1);

        {
            /********** output buf data to file **********/
            /* char* dstbuf = NULL;
            ret = gbd->lock(GRALLOC_USAGE_SW_WRITE_OFTEN, (void**)&dstbuf);
            output_buf_data_to_file(dstbuf, dstFormat, dstWidth, dstHeight, 0);
            ret = gbd->unlock(); */
            ptr = mpp_buffer_get_ptr(dst_buf);
            fwrite(ptr, 1, dst_size, fout);
        }
        printf("threadloop\n");
        break; // usleep(500000);
    } while(0);

END:
    if (src_frm)
        mpp_frame_deinit(&src_frm);

    if (dst_frm)
        mpp_frame_deinit(&dst_frm);

    if (src_buf)
        mpp_buffer_put(src_buf);

    if (dst_buf)
        mpp_buffer_put(dst_buf);

    if (fin)
        fclose(fin);

    if (fout)
        fclose(fout);

    RgaDeInit(&ctx);
    printf("rga3 test exit ret %d\n", ret);
    return 0;
}

// aarch64-rockchip930-linux-gnu-gcc test/rga3_test.c -o test/rga3_test.elf -I. -I../../../inc -lrockchip_mpp
