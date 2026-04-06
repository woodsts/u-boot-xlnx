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

#if IS_ENABLED(CONFIG_MTD_VIRT_CONCAT)

/**
 * mtd_virt_concat_node_create - List all concatenations found in DT
 *
 * Scans the device tree for nodes containing the "part-concat-next"
 * property and creates internal tracking structures for each
 * concatenation group.
 *
 * Return: 0 on success, -error otherwise.
 */
int mtd_virt_concat_node_create(void);

/**
 * mtd_virt_concat_add - Add an MTD device to the concat list
 * @mtd: pointer to mtd_info
 *
 * Checks if this MTD device's DT node matches any pending concatenation
 * group. If so, adds it as a sub-device.
 *
 * Return: true if the MTD was claimed by a concat group, false otherwise.
 */
bool mtd_virt_concat_add(struct mtd_info *mtd);

/**
 * mtd_virt_concat_create_join - Create and register concatenated MTD devices
 *
 * For each concatenation group that has all its sub-devices present,
 * creates and registers the concatenated MTD device.
 *
 * Return: 0 on success, -error otherwise.
 */
int mtd_virt_concat_create_join(void);

/**
 * mtd_virt_concat_destroy - Destroy concat that includes a specific MTD device
 * @mtd: pointer to mtd_info
 *
 * If the MTD device is part of a concatenated device, all other MTD devices
 * within that concat are registered individually. The concatenated device
 * is then removed.
 *
 * Return: 0 on success, -error otherwise.
 */
int mtd_virt_concat_destroy(struct mtd_info *mtd);

#else

static inline int mtd_virt_concat_node_create(void) { return 0; }
static inline bool mtd_virt_concat_add(struct mtd_info *mtd) { return false; }
static inline int mtd_virt_concat_create_join(void) { return 0; }
static inline int mtd_virt_concat_destroy(struct mtd_info *mtd) { return 0; }

#endif /* CONFIG_MTD_VIRT_CONCAT */
#endif /* MTD_CONCAT_H */
