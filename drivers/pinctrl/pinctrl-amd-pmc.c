// SPDX-License-Identifier: GPL-2.0
/*
 * AMD PMC Pin Controller Driver
 *
 * Copyright (C) 2026, Advanced Micro Devices, Inc.
 *
 * The AMD PMC (Platform Management Controller) provides pin multiplexing
 * for peripherals accessed via the PL-to-PMC AXI bridge. This driver
 * handles the IO_MODE and IO_CTRL registers for OSPI pin routing.
 *
 * IO_MODE register (offset 0x80):
 *   [2:0]  mode   - Mode selection (3 = OSPI)
 *   [8]    enable - Use software mode selection
 *
 * IO_CTRL register (offset 0xE8):
 *   [0]    dio_persist - Keep pin state across soft resets
 *
 * This driver uses regmap to share register access with sibling
 * clock and reset drivers under the same syscon parent.
 */

#define LOG_CATEGORY UCLASS_PINCTRL

#include <dm.h>
#include <log.h>
#include <regmap.h>
#include <syscon.h>

#include <dm/pinctrl.h>
#include <linux/bitops.h>
#include <linux/err.h>

/* Register offsets from PMC Global base */
#define PMC_IO_MODE		0x80
#define PMC_IO_CTRL		0xE8

#define PMC_IO_MODE_MASK	GENMASK(2, 0)
#define PMC_IO_MODE_OSPI	3
#define PMC_IO_MODE_ENABLE	BIT(8)

#define PMC_IO_CTRL_PERSIST	BIT(0)

struct amd_pmc_pinctrl_priv {
	struct regmap *regmap;
};

static int amd_pmc_pinctrl_set_state(struct udevice *dev, struct udevice *config)
{
	struct amd_pmc_pinctrl_priv *priv = dev_get_priv(dev);
	const char *groups, *function;

	groups = dev_read_string(config, "groups");
	function = dev_read_string(config, "function");

	log_debug("%s: groups=%s function=%s\n", __func__,
		  groups ? groups : "null", function ? function : "null");

	if (!groups || !function)
		return -EINVAL;

	if (strcmp(groups, "ospi") == 0 && strcmp(function, "ospi") == 0) {
		int ret;

		log_debug("%s: configuring OSPI pinmux\n", __func__);

		ret = regmap_update_bits(priv->regmap, PMC_IO_MODE,
					 PMC_IO_MODE_MASK | PMC_IO_MODE_ENABLE,
					 PMC_IO_MODE_OSPI | PMC_IO_MODE_ENABLE);
		if (ret)
			return ret;

		return regmap_update_bits(priv->regmap, PMC_IO_CTRL,
					  PMC_IO_CTRL_PERSIST, PMC_IO_CTRL_PERSIST);
	}

	log_warning("%s: unsupported group/function: %s/%s\n",
		    __func__, groups, function);

	return -EINVAL;
}

static const struct pinctrl_ops amd_pmc_pinctrl_ops = {
	.set_state = amd_pmc_pinctrl_set_state,
};

static int amd_pmc_pinctrl_probe(struct udevice *dev)
{
	struct amd_pmc_pinctrl_priv *priv = dev_get_priv(dev);

	log_debug("%s: %s\n", __func__, dev->name);

	priv->regmap = syscon_get_regmap(dev->parent);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	return 0;
}

static const struct udevice_id amd_pmc_pinctrl_ids[] = {
	{ .compatible = "amd,pmc-pinctrl" },
	{ }
};

U_BOOT_DRIVER(amd_pmc_pinctrl) = {
	.name		= "amd_pmc_pinctrl",
	.id		= UCLASS_PINCTRL,
	.of_match	= amd_pmc_pinctrl_ids,
	.ops		= &amd_pmc_pinctrl_ops,
	.probe		= amd_pmc_pinctrl_probe,
	.priv_auto	= sizeof(struct amd_pmc_pinctrl_priv),
};
