.. SPDX-License-Identifier: GPL-2.0

AMD MicroBlaze V Boot From QSPI/OSPI Flash Using preboot
========================================================

Overview
--------

AMD MicroBlaze V platforms can boot U-Boot from SPI flash and then use the
U-Boot ``preboot`` hook to continue booting a kernel image from flash.
This is useful when the board should always start from QSPI/OSPI flash
without relying on manual commands at the console.

MB-V support contains the pieces needed for this flow:

* ``configs/xilinx_mbv_spi.config`` enables the AXI QSPI path and sets
  ``CONFIG_SYS_SPI_U_BOOT_OFFS=0x10000`` by default.
* ``configs/xilinx_mbv_ospi.config`` enables the PMC OSPI path and sets
  ``CONFIG_SYS_SPI_U_BOOT_OFFS=0x20000`` by default.

``CONFIG_SYS_SPI_U_BOOT_OFFS`` is the flash offset from which SPL reads the
U-Boot FIT image. It must match the start address of the U-Boot FIT partition
in the actual flash layout. For the example layout shown below, override it
to ``0xA00000``.

On MB-V, ``preboot`` is the right place to run ``sf probe``, ``sf read``, and
then boot with either ``bootm`` or ``booti`` because it runs before the
bootdelay countdown and before the normal autoboot command.

Hardware support
----------------

MB-V can use either an AXI QSPI controller or a PMC OSPI controller for SPI
flash boot. At the U-Boot shell level, both are accessed through the ``sf``
command, so the user-visible boot steps are the same.

Building the board DTB
----------------------

Build the board device tree blob from the board DTS::

   $ dtc -I dts -O dtb system.dts -o system.dtb

Building OpenSBI
----------------

Clone the OpenSBI repository::

   $ git clone https://github.com/riscv-software-src/opensbi.git

Set the build variables::

   $ export CROSS_COMPILE=riscv64-amd-linux-gnu-
   $ export OPENSBI_LOAD_ADDR=0x80000000

Build OpenSBI::

   $ make -j8 PLATFORM=generic PLATFORM_RISCV_XLEN=64 FW_OPTIONS=2 \
       DEBUG=1 BUILD_INFO=y FW_PIC=y \
       FW_TEXT_START=${OPENSBI_LOAD_ADDR} FW_JUMP_ADDR=0x81200000

Export the path to
``build/platform/generic/firmware/fw_dynamic.bin`` for the U-Boot build::

   $ export OPENSBI=/path/to/opensbi/build/platform/generic/firmware/fw_dynamic.bin

Building U-Boot
---------------

The examples below use MB-V 64-bit. Before building U-Boot, configure the
compile-time ``preboot`` command.

Build for an AXI QSPI based design::

   $ make xilinx_mbv64_smode_defconfig xilinx_mbv_spi.config
   $ make -j32 EXT_DTB=system.dtb

Build for a PMC OSPI based design::

   $ make xilinx_mbv64_smode_defconfig xilinx_mbv_ospi.config
   $ make -j32 EXT_DTB=system.dtb

The important build outputs are:

* ``spl/u-boot-spl.bin``
* ``u-boot.itb``

SPL starts execution from BRAM at address ``0x0``.
When OpenSBI, the DTB, and U-Boot proper are packaged into the single
``u-boot.itb`` FIT image, SPL loads that FIT, selects the right
configuration, extracts the required images, places them at the required
addresses, and jumps to OpenSBI. OpenSBI then switches from M-mode to
S-mode and jumps to U-Boot proper.

Example flash layout
--------------------

An example flash layout is shown below:

* Partition ``0``: start ``0x0``, size ``0xA00000``, Bitstream + U-Boot SPL,
  approximate image size 4 MB, access RW.
* Partition ``1``: start ``0xA00000``, size ``0x200000``, U-Boot FIT image,
  approximate image size 1 MB, access RW.
* Partition ``2``: start ``0xC00000``, size ``0x40000``, U-Boot boot script,
  approximate image size 256 KB, access RW.
* Partition ``3``: start ``0xC40000``, size ``0x340000``, SPI U-Boot
  environment store, up to 3.25 MB, access RW.
* Partition ``4``: start ``0xF80000``, size ``0x2200000``, kernel
  uncompressed, approximate image size 34 MB, access RW.
* Partition ``5``: start ``0x3180000``, size ``0x4E80000``, root file system,
  up to 78.5 MB, access RW.

.. note::

   The U-Boot FIT image in Partition 1 starts at ``0xA00000``. Override
   ``CONFIG_SYS_SPI_U_BOOT_OFFS=0xA00000`` in the config fragment or via
   ``make menuconfig`` to match this layout. The defaults in
   ``xilinx_mbv_spi.config`` (``0x10000``) and ``xilinx_mbv_ospi.config``
   (``0x20000``) assume a minimal layout where the bitstream is absent or
   small; update the value to match the actual flash layout used.

For the direct MB-V ``preboot`` flow documented here, the key partition is the
kernel partition at ``0x0F80000``. The U-Boot FIT image remains at
``0x0A00000``, and the other partitions can be used as required by the
platform design.

Enabling preboot
----------------

Because ``preboot`` runs before the user reaches the console, configure the
full boot command before building U-Boot. The simplest approach is to use
fixed literal addresses and offsets in ``CONFIG_PREBOOT``.

Enable ``preboot`` at compile time for a FIT image. The value must be a
single quoted string on one line in the defconfig or config fragment file::

   CONFIG_USE_PREBOOT=y
   CONFIG_PREBOOT="sf probe 0 0 0; sf read 0x84000000 0x0F80000 0x02200000; sf read 0x90000000 0x3180000 0x4E80000; bootm 0x84000000 0x90000000:0x4E80000 ${fdtcontroladdr}"

For a raw Linux ``Image``, enable it at compile time with::

   CONFIG_USE_PREBOOT=y
   CONFIG_PREBOOT="sf probe 0 0 0; sf read 0x84000000 0x0F80000 0x02200000; sf read 0x90000000 0x3180000 0x4E80000; booti 0x84000000 0x90000000:0x4E80000 ${fdtcontroladdr}"

``CONFIG_USE_PREBOOT`` is defined in ``boot/Kconfig``. When it is enabled,
U-Boot checks the ``preboot`` environment variable immediately before the
bootdelay countdown starts.

Preparing the flash payload
---------------------------

MB-V can boot either a FIT image with ``bootm`` or a raw Linux ``Image``
with ``booti`` after reading it into memory with ``sf read``.

Typical values for that flow are:

* kernel load address: ``0x84000000``
* kernel flash offset: ``0x0F80000``
* kernel size: ``0x02200000``
* ramdisk load address: ``0x90000000``
* ramdisk flash offset: ``0x3180000``
* ramdisk size: ``0x4E80000``
* for ``bootm`` and ``booti``, use ``${fdtcontroladdr}`` as the FDT argument

Program the payload images into flash before first boot. The example below
assumes the kernel and ramdisk have already been loaded into RAM (for example
via TFTP or XSDB) at their respective load addresses::

   sf probe 0 0 0
   sf erase 0x0F80000 0x02200000
   sf write 0x84000000 0x0F80000 0x02200000
   sf erase 0x3180000 0x4E80000
   sf write 0x90000000 0x3180000 0x4E80000

At boot time, ``preboot`` can then execute one of the following:

For a FIT image::

   sf probe 0 0 0
   sf read 0x84000000 0x0F80000 0x02200000
   sf read 0x90000000 0x3180000 0x4E80000
   bootm 0x84000000 0x90000000:0x4E80000 ${fdtcontroladdr}

For a raw Linux ``Image``::

   sf probe 0 0 0
   sf read 0x84000000 0x0F80000 0x02200000
   sf read 0x90000000 0x3180000 0x4E80000
   booti 0x84000000 0x90000000:0x4E80000 ${fdtcontroladdr}

Boot flow summary
-----------------

With the configuration above, the boot flow is:

#. The FPGA bitstream initializes the design and includes SPL in BRAM at
   address ``0x0``.
#. SPL loads ``u-boot.itb`` from ``CONFIG_SYS_SPI_U_BOOT_OFFS``, selects the
   FIT configuration, extracts the required images, and jumps to OpenSBI.
#. OpenSBI switches from M-mode to S-mode and jumps to U-Boot proper.
#. U-Boot proper starts and evaluates ``preboot``.
#. ``preboot`` probes the SPI flash with ``sf probe``.
#. ``preboot`` reads the kernel and ramdisk images from flash with
   ``sf read``.
#. ``preboot`` boots the loaded image with ``bootm`` or ``booti``.

This keeps the flash boot policy in the ``preboot`` command while still using
the normal U-Boot MB-V SPI flash boot support.
