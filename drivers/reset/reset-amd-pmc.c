// SPDX-License-Identifier: GPL-2.0
/*
 * AMD PMC Reset Controller Driver
 *
 * Copyright (C) 2026, Advanced Micro Devices, Inc.
 *
 * The AMD PMC (Platform Management Controller) provides reset control
 * for peripherals accessed via the PL-to-PMC AXI bridge. Each reset
 * is controlled by bit 0 of a dedicated register.
 *
 * This driver uses regmap to share register access with sibling
 * clock and pinctrl drivers under the same syscon parent.
 */

#define LOG_CATEGORY UCLASS_RESET

#include <dm.h>
#include <log.h>
#include <regmap.h>
#include <reset-uclass.h>
#include <syscon.h>

#include <linux/bitops.h>

/* Reset register offsets from PMC Global base */
#define PMC_RST_PMCL		0x50
#define PMC_RST_DMA		0x54
#define PMC_RST_OSPI		0x58
#define PMC_RST_SBI		0x5c
#define PMC_RST_CCU		0x60
#define PMC_RST_PUF		0x64

#define PMC_RESET_BIT		BIT(0)

/* Reset IDs */
#define PMC_RESET_PMCL		0
#define PMC_RESET_DMA		1
#define PMC_RESET_OSPI		2
#define PMC_RESET_SBI		3
#define PMC_RESET_CCU		4
#define PMC_RESET_PUF		5

struct amd_pmc_reset_priv {
	struct regmap *regmap;
};

static const u32 amd_pmc_reset_offsets[] = {
	[PMC_RESET_PMCL] = PMC_RST_PMCL,
	[PMC_RESET_DMA] = PMC_RST_DMA,
	[PMC_RESET_OSPI] = PMC_RST_OSPI,
	[PMC_RESET_SBI] = PMC_RST_SBI,
	[PMC_RESET_CCU] = PMC_RST_CCU,
	[PMC_RESET_PUF] = PMC_RST_PUF,
};

static int amd_pmc_reset_request(struct reset_ctl *rst)
{
	log_debug("%s: id=%lu\n", __func__, rst->id);

	if (rst->id >= ARRAY_SIZE(amd_pmc_reset_offsets))
		return -EINVAL;

	return 0;
}

static int amd_pmc_reset_assert(struct reset_ctl *rst)
{
	struct amd_pmc_reset_priv *priv = dev_get_priv(rst->dev);
	u32 offset = amd_pmc_reset_offsets[rst->id];

	log_debug("%s: id=%lu offset=0x%x\n", __func__, rst->id, offset);

	return regmap_update_bits(priv->regmap, offset, PMC_RESET_BIT,
				  PMC_RESET_BIT);
}

static int amd_pmc_reset_deassert(struct reset_ctl *rst)
{
	struct amd_pmc_reset_priv *priv = dev_get_priv(rst->dev);
	u32 offset = amd_pmc_reset_offsets[rst->id];

	log_debug("%s: id=%lu offset=0x%x\n", __func__, rst->id, offset);

	return regmap_update_bits(priv->regmap, offset, PMC_RESET_BIT, 0);
}

static const struct reset_ops amd_pmc_reset_ops = {
	.request = amd_pmc_reset_request,
	.rst_assert = amd_pmc_reset_assert,
	.rst_deassert = amd_pmc_reset_deassert,
};

static int amd_pmc_reset_probe(struct udevice *dev)
{
	struct amd_pmc_reset_priv *priv = dev_get_priv(dev);

	log_debug("%s: %s\n", __func__, dev->name);

	priv->regmap = syscon_get_regmap(dev->parent);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	return 0;
}

static const struct udevice_id amd_pmc_reset_ids[] = {
	{ .compatible = "amd,pmc-reset" },
	{ }
};

U_BOOT_DRIVER(amd_pmc_reset) = {
	.name		= "amd_pmc_reset",
	.id		= UCLASS_RESET,
	.of_match	= amd_pmc_reset_ids,
	.ops		= &amd_pmc_reset_ops,
	.probe		= amd_pmc_reset_probe,
	.priv_auto	= sizeof(struct amd_pmc_reset_priv),
};
