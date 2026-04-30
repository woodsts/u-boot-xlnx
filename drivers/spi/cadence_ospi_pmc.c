// SPDX-License-Identifier: GPL-2.0
/*
 * Cadence OSPI driver - PMC (Platform Management Controller) platform support
 *
 * Copyright (C) 2026, Advanced Micro Devices, Inc.
 *
 * The Cadence OSPI controller lives in the PMC (Platform Management
 * Controller) subsystem and is accessed from the PL (Programmable Logic)
 * through a PL->PMC AXI32 bridge which exposes:
 *   0x04090000  Cadence OSPI APB registers  (STIG path, always reachable)
 *
 * The AHB data window required by DAC/indirect/DMA paths is not reliably
 * connected through this bridge, so all bulk data transfer uses the STIG
 * (Software Triggered Instruction Generator) path via APB registers.
 *
 * Clock, reset, and pin muxing are handled via DT-based frameworks:
 *   - Reset: via reset controller (amd,pmc-reset), cycled via reset_reset()
 *   - Clock: via clock controller (amd,pmc-clk), enabled in cadence_qspi.c
 *   - Pins: via pin controller (amd,pmc-pinctrl), handled by DM
 *
 * The PL-to-PMC AXI bridge is enabled by the PDI during boot.
 */

#include "cadence_qspi.h"

int cadence_qspi_pmc_ctrl_reset(struct cadence_spi_priv *priv)
{
	if (!priv->resets)
		return -ENOENT;

	return reset_reset(priv->resets->resets);
}
