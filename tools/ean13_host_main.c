/* Host 测试入口：从 argv[3] 文件读 raw 灰度（w*h 字节），调用
 * ean13_decode_frame，结果写到 stdout（成功打印 13 位数字，失败打印 FAIL）。
 * 用文件而非 stdin：Windows 下子进程 stdin 在 ~144KB 处截断，无法承载
 * VGA 帧（640×480=307KB）。
 *
 * 编译（与固件相同源码，仅加本入口）：
 *   gcc -I src/middleware/inc tools/ean13_host_main.c src/middleware/src/barcode_1d.c -o ean13_host
 * 用法：
 *   ean13_host <width> <height> <input.raw>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "barcode_1d.h"

int main(int argc, char ** argv)
{
    if (argc < 4)
    {
        fprintf(stderr, "usage: %s <width> <height> <input.raw>\n", argv[0]);
        return 2;
    }
    uint32_t const w = (uint32_t) strtoul(argv[1], NULL, 10);
    uint32_t const h = (uint32_t) strtoul(argv[2], NULL, 10);
    if ((w == 0U) || (h == 0U) || ((size_t) w * h > (2048U * 1024U)))
    {
        fprintf(stderr, "bad size\n");
        return 2;
    }

    FILE * p_file = fopen(argv[3], "rb");
    if (NULL == p_file)
    {
        fprintf(stderr, "cannot open %s\n", argv[3]);
        return 2;
    }
    uint8_t * p_gray = (uint8_t *) malloc((size_t) w * h);
    if (NULL == p_gray)
    {
        fclose(p_file);
        return 2;
    }
    size_t const got = fread(p_gray, 1, (size_t) w * h, p_file);
    fclose(p_file);
    if (got != (size_t) w * h)
    {
        fprintf(stderr, "short read %lu of %lu\n", (unsigned long) got, (unsigned long) ((size_t) w * h));
        free(p_gray);
        return 2;
    }

    char text[EAN13_TEXT_MAX];
    if (ean13_decode_frame(p_gray, w, h, text))
    {
        printf("%s\n", text);
        free(p_gray);
        return 0;
    }
    printf("FAIL\n");
    free(p_gray);
    return 1;
}
