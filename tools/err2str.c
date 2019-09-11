/*
 * Copyright (c) 2019 VacingFang
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdio.h>
#include "libavutil/error.h"
#define VRED "\033[31m"
#define VEND "\033[m"

static void usage(char *name)
{
    printf("Simple av_err2str() tool, convert error number to string");
    printf(" usage: %s errornum\n", name);
    printf("    eg: %s -875574520\n", name);
}

int main(int argc, char **argv)
{
    int errnum = 0;
    int ret = 0;

    if (argc != 2) {
        usage(argv[0]);
        return -1;
    }

    ret = sscanf(argv[1], "%d", &errnum);
    if (ret <= 0) {
        usage(argv[0]);
        printf("Input command is [%s %s]", argv[0], argv[1]);
        return -1;
    }
    printf("input error num=" VRED "%d" VEND "\n", errnum);
    printf("message=[" VRED "%s" VEND "]\n", av_err2str(errnum));
    return 0;
}
