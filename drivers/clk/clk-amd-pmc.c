// SPDX-License-Identifier: GPL-2.0
/*
 * AMD PMC Clock Controller Driver
 *
 * Copyright (C) 2026, Advanced Micro Devices, Inc.
 *
 * The AMD PMC (Platform Management Controller) provides clock control
 * for peripherals accessed via the PL-to-PMC AXI bridge. Each clock
 * register contains SRCSEL, DIVISOR, and CLKACT fields.
 *
 * Clock register layout:
 *   [0]     SRCSEL  - 0=oscillator/divisor, 1=EMCCLK
 *   [13:8]  DIVISOR - 6-bit divider (only when SRCSEL=0)
 *   [25]    CLKACT  - Clock active enable
 *
 * This driver uses regmap to share register access with sibling
 * reset and pinctrl drivers under the same syscon parent.
 */

#define LOG_CATEGORY UCLASS_CLK

#include <clk-uclass.h>
#include <dm.h>
#include <log.h>
#include <regmap.h>
#include <syscon.h>

#include <linux/bitops.h>

/* Clock register offsets from PMC Global base */
#define PMC_CLK_PMCL_MAIN_IRO	0x30
#define PMC_CLK_CCU_IRO		0x34
#define PMC_CLK_OSPI		0x38
#define PMC_CLK_EFUSE		0x3c

#define PMC_CLK_SRCSEL_MASK	BIT(0)
#define PMC_CLK_DIV_SHIFT	8
#define PMC_CLK_DIV_MASK	GENMASK(13, 8)
#define PMC_CLK_CLKACT		BIT(25)

/* Clock IDs */
#define PMC_CLK_ID_PMCL_MAIN_IRO	0
#define PMC_CLK_ID_CCU_IRO		1
#define PMC_CLK_ID_OSPI			2
#define PMC_CLK_ID_EFUSE		3

#define PMC_CLK_DIV_MAX		63

struct amd_pmc_clk_priv {
	struct regmap *regmap;
	unsigned long ref_rate;
	unsigned long baud_rate_clk;
};

static const u32 amd_pmc_clk_offsets[] = {
	[PMC_CLK_ID_PMCL_MAIN_IRO] = PMC_CLK_PMCL_MAIN_IRO,
	[PMC_CLK_ID_CCU_IRO] = PMC_CLK_CCU_IRO,
	[PMC_CLK_ID_OSPI] = PMC_CLK_OSPI,
	[PMC_CLK_ID_EFUSE] = PMC_CLK_EFUSE,
};

static int amd_pmc_clk_request(struct clk *clk)
{
	log_debug("%s: id=%lu\n", __func__, clk->id);

	if (clk->id >= ARRAY_SIZE(amd_pmc_clk_offsets))
		return -EINVAL;

	return 0;
}

static int amd_pmc_clk_enable(struct clk *clk)
{
	struct amd_pmc_clk_priv *priv = dev_get_priv(clk->dev);
	u32 offset = amd_pmc_clk_offsets[clk->id];
	u32 reg, div;

	log_debug("%s: id=%lu offset=0x%x\n", __func__, clk->id, offset);

	regmap_read(priv->regmap, offset, &reg);

	div = (reg & PMC_CLK_DIV_MASK) >> PMC_CLK_DIV_SHIFT;
	if (div == 0)
		div = 1;

	priv->baud_rate_clk = priv->ref_rate / div;

	return regmap_update_bits(priv->regmap, offset, PMC_CLK_CLKACT,
				  PMC_CLK_CLKACT);
}

static int amd_pmc_clk_disable(struct clk *clk)
{
	struct amd_pmc_clk_priv *priv = dev_get_priv(clk->dev);
	u32 offset = amd_pmc_clk_offsets[clk->id];

	log_debug("%s: id=%lu offset=0x%x\n", __func__, clk->id, offset);

	return regmap_update_bits(priv->regmap, offset, PMC_CLK_CLKACT, 0);
}

static ulong amd_pmc_clk_get_rate(struct clk *clk)
{
	struct amd_pmc_clk_priv *priv = dev_get_priv(clk->dev);
	u32 offset = amd_pmc_clk_offsets[clk->id];
	u32 reg, div;
	ulong rate;
	int ret;

	log_debug("%s: id=%lu offset=0x%x\n", __func__, clk->id, offset);

	ret = regmap_read(priv->regmap, offset, &reg);
	if (ret)
		return 0;

	if (reg & PMC_CLK_SRCSEL_MASK) {
		rate = priv->ref_rate;
	} else {
		div = (reg & PMC_CLK_DIV_MASK) >> PMC_CLK_DIV_SHIFT;
		if (div == 0)
			div = 1;
		rate = priv->baud_rate_clk * div;
	}

	log_debug("%s: rate=%lu\n", __func__, rate);

	return rate;
}

static ulong amd_pmc_clk_set_rate(struct clk *clk, ulong rate)
{
	struct amd_pmc_clk_priv *priv = dev_get_priv(clk->dev);
	u32 offset = amd_pmc_clk_offsets[clk->id];
	u32 div;
	int ret;

	log_debug("%s: id=%lu rate=%lu\n", __func__, clk->id, rate);

	if (rate == 0 || rate > priv->ref_rate)
		return -EINVAL;

	div = priv->ref_rate / rate;
	if (div > PMC_CLK_DIV_MAX)
		div = PMC_CLK_DIV_MAX;
	if (div == 0)
		div = 1;

	priv->baud_rate_clk = priv->ref_rate / div;

	log_debug("%s: div=%u actual_rate=%lu\n", __func__, div, priv->ref_rate / div);

	ret = regmap_update_bits(priv->regmap, offset,
				 PMC_CLK_DIV_MASK | PMC_CLK_SRCSEL_MASK,
				 (div << PMC_CLK_DIV_SHIFT));
	if (ret)
		return ret;

	return priv->ref_rate / div;
}

static const struct clk_ops amd_pmc_clk_ops = {
	.request = amd_pmc_clk_request,
	.enable = amd_pmc_clk_enable,
	.disable = amd_pmc_clk_disable,
	.get_rate = amd_pmc_clk_get_rate,
	.set_rate = amd_pmc_clk_set_rate,
};

static int amd_pmc_clk_probe(struct udevice *dev)
{
	struct amd_pmc_clk_priv *priv = dev_get_priv(dev);
	struct clk ref_clk;
	int ret;

	log_debug("%s: %s\n", __func__, dev->name);

	priv->regmap = syscon_get_regmap(dev->parent);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	ret = clk_get_by_name(dev->parent, "ref", &ref_clk);
	if (ret)
		return ret;

	priv->ref_rate = clk_get_rate(&ref_clk);
	if (!priv->ref_rate) {
		log_err("%s: failed to get ref clock rate\n", __func__);
		return -EINVAL;
	}

	log_debug("%s: ref_rate=%lu\n", __func__, priv->ref_rate);

	return 0;
}

static const struct udevice_id amd_pmc_clk_ids[] = {
	{ .compatible = "amd,pmc-clk" },
	{ }
};

U_BOOT_DRIVER(amd_pmc_clk) = {
	.name		= "amd_pmc_clk",
	.id		= UCLASS_CLK,
	.of_match	= amd_pmc_clk_ids,
	.ops		= &amd_pmc_clk_ops,
	.probe		= amd_pmc_clk_probe,
	.priv_auto	= sizeof(struct amd_pmc_clk_priv),
};
