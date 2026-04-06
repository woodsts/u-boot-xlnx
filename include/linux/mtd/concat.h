/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * MTD device concatenation layer definitions
 *
 * Copyright © 2002      Robert Kaiser <rkaiser@sysgo.de>
 *
 */

#ifndef MTD_CONCAT_H
#define MTD_CONCAT_H

#include <linux/mtd/mtd.h>

/*
 * Our storage structure:
 * Subdev points to an array of pointers to struct mtd_info objects
 * which is allocated along with this structure
 */
struct mtd_concat {
	struct mtd_info mtd;
	int num_subdev;
	struct mtd_info **subdev;
};

struct mtd_info *mtd_concat_create(
    struct mtd_info *subdev[],  /* subdevices to concatenate */
    int num_devs,               /* number of subdevices      */
#ifndef __UBOOT__
    const char *name);          /* name for the new device   */
#else
    char *name);          /* name for the new device   */
#endif

void mtd_concat_destroy(struct mtd_info *mtd);

#endif
