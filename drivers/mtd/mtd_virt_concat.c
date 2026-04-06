// SPDX-License-Identifier: GPL-2.0+
#define LOG_CATEGORY UCLASS_MTD
/*
 * Virtual concat MTD device driver for U-Boot
 *
 * Copyright (C) 2018 Bernhard Frauendienst
 * Author: Bernhard Frauendienst <kernel@nospam.obeliks.de>
 *
 * Copyright (C) 2026 Advanced Micro Devices, Inc.
 * Author: Amit Kumar Mahapatra <amit.kumar-mahapatra@amd.com>
 *
 * This driver enables transparent concatenation of MTD partitions
 * based on the "part-concat-next" Device Tree property. It is used
 * for QSPI/OSPI stacked memory configurations where a single
 * filesystem spans multiple flash devices.
 */

#include <dm.h>
#include <malloc.h>

#include <dm/ofnode.h>
#include <linux/compat.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/mtd/concat.h>
#include <linux/mtd/mtd.h>

#define CONCAT_PROP "part-concat-next"
#define CONCAT_POSTFIX "concat"
#define MIN_DEV_PER_CONCAT 2
#define CONCAT_MAX_CHAIN 64

static LIST_HEAD(concat_node_list);

/**
 * struct mtd_virt_concat_node - components of a concatenation
 * @head:       List handle
 * @count:      Number of nodes in this concatenation group
 * @num_subdev: Number of sub-devices collected so far
 * @nodes:      Array of ofnode references to the partitions to concatenate
 * @subdev:     Collected sub-device pointers (used before concat creation)
 * @mtd:        MTD device returned by mtd_concat_create() (set after join)
 */
struct mtd_virt_concat_node {
	struct list_head head;
	unsigned int count;
	unsigned int num_subdev;
	ofnode *nodes;
	struct mtd_info **subdev;
	struct mtd_info *mtd;
};

/* Forward declaration - destroy path calls this before its definition */
static void mtd_virt_concat_release_subdevs(struct mtd_virt_concat_node *item);

/**
 * mtd_get_node - Get the ofnode associated with an MTD device
 * @mtd: pointer to mtd_info
 *
 * In U-Boot, MTD devices can have their DT node stored in either
 * mtd->dev (via mtd_get_ofnode) or mtd->flash_node.
 *
 * Return: the ofnode for this MTD device, or ofnode_null() if none.
 */
static ofnode mtd_get_node(struct mtd_info *mtd)
{
	if (IS_ENABLED(CONFIG_DM) && mtd->dev)
		return mtd_get_ofnode(mtd);
	if (ofnode_valid(mtd->flash_node))
		return mtd->flash_node;

	return ofnode_null();
}

/**
 * mtd_is_part_concat - Check if a node is already part of a concat group
 * @node: ofnode to check
 *
 * Return: true if the node is already claimed by a concatenation group.
 */
static bool mtd_is_part_concat(ofnode node)
{
	struct mtd_virt_concat_node *item;
	int idx;

	if (!ofnode_valid(node))
		return false;

	list_for_each_entry(item, &concat_node_list, head) {
		for (idx = 0; idx < item->count; idx++) {
			if (ofnode_equal(item->nodes[idx], node))
				return true;
		}
	}

	return false;
}

/**
 * mtd_virt_concat_put_mtd_devices - Release references on sub-devices
 * @concat: pointer to mtd_concat structure
 *
 * Calls put_mtd_device() for each sub-device to decrement use counts.
 */
static void mtd_virt_concat_put_mtd_devices(struct mtd_concat *concat)
{
	int i;

	for (i = 0; i < concat->num_subdev; i++)
		put_mtd_device(concat->subdev[i]);
}

int mtd_virt_concat_destroy(struct mtd_info *mtd)
{
	struct mtd_virt_concat_node *item, *tmp;
	struct mtd_concat *concat;
	struct mtd_info *child;
	bool is_mtd_found;
	int idx, ret = 0;
	ofnode mtd_node;

	mtd_node = mtd_get_node(mtd);
	if (!ofnode_valid(mtd_node))
		return 0;

	list_for_each_entry_safe(item, tmp, &concat_node_list, head) {
		is_mtd_found = false;

		/* Find the concat item that holds the mtd device */
		for (idx = 0; idx < item->count; idx++) {
			if (ofnode_equal(item->nodes[idx], mtd_node)) {
				is_mtd_found = true;
				break;
			}
		}
		if (!is_mtd_found)
			continue;

		if (!item->mtd) {
			mtd_virt_concat_release_subdevs(item);
			goto cleanup;
		}

		concat = (struct mtd_concat *)item->mtd;

		/*
		 * Since this concatenated device is being removed, retrieve
		 * all MTD devices that are part of it and register them
		 * individually.
		 */
		for (idx = 0; idx < concat->num_subdev; idx++) {
			child = concat->subdev[idx];
			if (!ofnode_equal(mtd_get_node(child), mtd_node)) {
				ret = add_mtd_device(child);
				if (ret)
					goto out;
			}
		}

		/* Destroy the concat */
		del_mtd_device(item->mtd);
		kfree(item->mtd->name);
		mtd_virt_concat_put_mtd_devices(concat);
		mtd_concat_destroy(item->mtd);

cleanup:
		kfree(item->nodes);
		list_del(&item->head);
		kfree(item);
	}

	return 0;

out:
	return ret;
}

/**
 * mtd_virt_concat_create_item - Create a concat tracking item
 * @parts:  ofnode of the partition that declares part-concat-next
 * @count:  total number of partitions in this concatenation group
 *
 * Allocates and initializes a concat_node structure, populating
 * it with the ofnodes of all partitions in the group.
 *
 * Return: 0 on success, -error otherwise.
 */
static int mtd_virt_concat_create_item(ofnode parts, unsigned int count)
{
	struct mtd_virt_concat_node *item;
	ofnode cur = parts;
	int i;

	/* Check if any target node is already part of another concat */
	for (i = 0; i < count - 1; i++) {
		cur = ofnode_parse_phandle(cur, CONCAT_PROP, 0);
		if (!ofnode_valid(cur) || mtd_is_part_concat(cur))
			return 0;
	}

	item = kzalloc(sizeof(*item), GFP_KERNEL);
	if (!item)
		return -ENOMEM;

	item->count = count;
	item->nodes = kcalloc(count, sizeof(*item->nodes), GFP_KERNEL);
	if (!item->nodes) {
		kfree(item);
		return -ENOMEM;
	}

	/*
	 * The partition in which "part-concat-next" property
	 * is defined is the first device in the list of concat
	 * devices.
	 */
	item->nodes[0] = parts;

	for (i = 1; i < count; i++) {
		item->nodes[i] = ofnode_parse_phandle(item->nodes[i - 1],
						      CONCAT_PROP, 0);
		if (!ofnode_valid(item->nodes[i])) {
			kfree(item->nodes);
			kfree(item);
			return -EINVAL;
		}
	}

	item->subdev = kcalloc(count, sizeof(*item->subdev), GFP_KERNEL);
	if (!item->subdev) {
		kfree(item->nodes);
		kfree(item);
		return -ENOMEM;
	}

	list_add_tail(&item->head, &concat_node_list);

	return 0;
}

/**
 * mtd_virt_concat_release_subdevs - Release intercepted unregistered slaves
 * @item: concat tracking item whose concat was never formed
 *
 * Each slave in item->subdev[] was intercepted by mtd_virt_concat_add(),
 * which bumped its usecount via __get_mtd_device() and withheld it from
 * add_mtd_device().  Undo the usecount increment and free the partition
 * allocation.  Must not be called after the concat has been formed (i.e.
 * item->mtd is set), as the concat then owns the subdev array.
 */
static void mtd_virt_concat_release_subdevs(struct mtd_virt_concat_node *item)
{
	int i;

	for (i = 0; i < item->count; i++) {
		if (!item->subdev[i])
			continue;
		__put_mtd_device(item->subdev[i]);
		kfree(item->subdev[i]->name);
		kfree(item->subdev[i]);
	}
	kfree(item->subdev);
	item->subdev = NULL;
}

/**
 * mtd_virt_concat_destroy_items - Free all concat tracking items
 *
 * Called on error paths to clean up partially created items.
 */
static void mtd_virt_concat_destroy_items(void)
{
	struct mtd_virt_concat_node *item, *temp;

	list_for_each_entry_safe(item, temp, &concat_node_list, head) {
		mtd_virt_concat_release_subdevs(item);
		kfree(item->nodes);
		list_del(&item->head);
		kfree(item);
	}
}

bool mtd_virt_concat_add(struct mtd_info *mtd)
{
	struct mtd_virt_concat_node *item;
	ofnode mtd_node;
	int idx;

	mtd_node = mtd_get_node(mtd);
	if (!ofnode_valid(mtd_node))
		return false;

	list_for_each_entry(item, &concat_node_list, head) {
		for (idx = 0; idx < item->count; idx++) {
			if (ofnode_equal(item->nodes[idx], mtd_node)) {
				if (__get_mtd_device(mtd))
					return false;
				item->subdev[idx] = mtd;
				item->num_subdev++;
				return true;
			}
		}
	}

	return false;
}

/**
 * ofnode_dfs_next - Return the next node in depth-first traversal order
 * @node: current ofnode
 *
 * Return: next ofnode in DFS order, or ofnode_null() at end of tree.
 */
static ofnode ofnode_dfs_next(ofnode node)
{
	ofnode child, sibling;

	child = ofnode_first_subnode(node);
	if (ofnode_valid(child))
		return child;

	while (ofnode_valid(node)) {
		sibling = ofnode_next_subnode(node);
		if (ofnode_valid(sibling))
			return sibling;
		node = ofnode_get_parent(node);
	}

	return ofnode_null();
}

/**
 * ofnode_find_node_with_property - Find a node with a given property
 * @from: starting ofnode (use ofnode_null() to start from root)
 * @prop_name: property name to search for
 *
 * Walks all nodes in depth-first order looking for nodes that have
 * the specified property. Works with both flattened and live DT.
 *
 * Return: the first matching ofnode after @from, or ofnode_null() if none.
 */
static ofnode ofnode_find_node_with_property(ofnode from, const char *prop_name)
{
	ofnode node;

	node = ofnode_valid(from) ? ofnode_dfs_next(from) : ofnode_root();

	for (; ofnode_valid(node); node = ofnode_dfs_next(node)) {
		if (ofnode_has_property(node, prop_name))
			return node;
	}

	return ofnode_null();
}

int mtd_virt_concat_node_create(void)
{
	int ret = 0, count = 0, depth;
	ofnode parts = ofnode_null();
	ofnode next;

	/* Discard any leftover state from a previous (possibly partial) scan */
	mtd_virt_concat_destroy_items();

	/* List all the concatenations found in DT */
	do {
		parts = ofnode_find_node_with_property(parts, CONCAT_PROP);
		if (!ofnode_valid(parts))
			break;

		if (!ofnode_is_enabled(parts))
			continue;

		if (mtd_is_part_concat(parts))
			continue;

		/*
		 * Count the concat chain by following part-concat-next
		 * links. We support a simple chain (single phandle per node).
		 */
		count = 1;
		next = parts;
		for (depth = 0; depth < CONCAT_MAX_CHAIN; depth++) {
			next = ofnode_parse_phandle(next, CONCAT_PROP, 0);
			if (!ofnode_valid(next))
				break;
			if (mtd_is_part_concat(next))
				break;
			count++;
		}

		if (count < MIN_DEV_PER_CONCAT)
			continue;

		ret = mtd_virt_concat_create_item(parts, count);
		if (ret)
			goto destroy_items;
	} while (ofnode_valid(parts));

	return ret;

destroy_items:
	mtd_virt_concat_destroy_items();

	return ret;
}

int mtd_virt_concat_create_join(void)
{
	struct mtd_virt_concat_node *item, *tmp;
	int ret, idx, offset;
	struct mtd_info *mtd;
	size_t name_sz;
	char *name;

	list_for_each_entry(item, &concat_node_list, head) {
		/*
		 * Skip if not all MTD devices for this concatenation are
		 * present yet, or if the concat device was already created.
		 */
		if (item->count != item->num_subdev)
			continue;

		if (item->mtd)
			continue;

		/* Calculate the length of the name of the virtual device */
		name_sz = 0;
		for (idx = 0; idx < item->num_subdev; idx++)
			name_sz += strlen(item->subdev[idx]->name) + 1;
		name_sz += strlen(CONCAT_POSTFIX);

		name = kmalloc(name_sz + 1, GFP_KERNEL);
		if (!name) {
			ret = -ENOMEM;
			goto err_rollback;
		}

		offset = 0;
		for (idx = 0; idx < item->num_subdev; idx++)
			offset += snprintf(name + offset, name_sz + 1 - offset,
					   "%s-", item->subdev[idx]->name);
		snprintf(name + offset, name_sz + 1 - offset, CONCAT_POSTFIX);

		mtd = mtd_concat_create(item->subdev, item->num_subdev, name);
		if (!mtd) {
			kfree(name);
			ret = -ENXIO;
			goto err_rollback;
		}

		/*
		 * Store the pointer directly. item->subdev is no longer
		 * needed; the concat owns its own copy of the subdev array.
		 */
		item->mtd = mtd;
		kfree(item->subdev);
		item->subdev = NULL;

		/* Add the mtd device */
		ret = add_mtd_device(item->mtd);
		if (ret) {
			/*
			 * This concat was never registered; destroy it without
			 * calling del_mtd_device().
			 */
			kfree(item->mtd->name);
			mtd_virt_concat_put_mtd_devices((struct mtd_concat *)item->mtd);
			mtd_concat_destroy(item->mtd);
			item->mtd = NULL;
			goto err_rollback;
		}
	}

	return 0;

err_rollback:
	/*
	 * Unregister and destroy all concat devices that were successfully
	 * registered in earlier iterations, then free all tracking structures.
	 */
	list_for_each_entry_safe(item, tmp, &concat_node_list, head) {
		if (item->mtd) {
			del_mtd_device(item->mtd);
			kfree(item->mtd->name);
			mtd_virt_concat_put_mtd_devices((struct mtd_concat *)item->mtd);
			mtd_concat_destroy(item->mtd);
		} else {
			/*
			 * Concat was never formed: release each intercepted
			 * slave that was withheld from add_mtd_device().
			 */
			mtd_virt_concat_release_subdevs(item);
		}
		kfree(item->nodes);
		list_del(&item->head);
		kfree(item);
	}

	return ret;
}
