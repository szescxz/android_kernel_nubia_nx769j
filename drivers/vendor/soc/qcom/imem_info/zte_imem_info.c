// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Platform driver for reading vendor extended imem info
 *
 * when                     who               what
 * 2018-03-13            wangzy          initialized
 *
 *
 *
 */

/*=========================================
*  Head Files :
* =========================================
*/
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of.h>
#include "zte_imem_info.h"

/*==========================================================================
*  Variables :
* ==========================================================================
*/
static void __iomem *vendor_imem_info_addr;


/*==========================================================================
*  Functions :
* ==========================================================================
*/
static int vendor_imem_info_parse_dt(const char *compatible)
{
	struct device_node *np;
	int val = -1;

	np = of_find_compatible_node(NULL, NULL, compatible);
	if (!np) {
		pr_err("unable to find DT imem %s node\n", compatible);
	} else {
		vendor_imem_info_addr = of_iomap(np, 0);
		if (!vendor_imem_info_addr) {
			pr_err("unable to map imem %s offset\n", compatible);
		} else {
			val = __raw_readl(vendor_imem_info_addr);
			pr_info("%s: 0x%x\n", compatible, val);
		}
	}
	return val;
}

#ifdef CONFIG_NUBIA_BOARD_ID
extern int nubia_get_hw_id(void);
#endif

int request_board_id(void)
{
	int id = 0;
#ifdef CONFIG_NUBIA_BOARD_ID
	id = nubia_get_hw_id();
#else
	id = vendor_imem_info_parse_dt("qcom,msm-imem-board-id");
#endif

	return id;
}
EXPORT_SYMBOL(request_board_id);

static int ddr_id_read_proc(struct seq_file *m, void *v)
{
	char *manufacturer_name = NULL;
	char *device_type = NULL;
	int size = 0;

	switch (vendor_imem_info_parse_dt("qcom,msm-imem-ddr_memory_manufacture")) {
	case SAMSUNG:
		manufacturer_name = "SAMSUNG";
		break;
	case QIMONDA:
		manufacturer_name = "QIMONDA";
		break;
	case ELPIDA:
		manufacturer_name = "ELPIDA";
		break;
	case ETRON:
		manufacturer_name = "ETRON";
		break;
	case NANYA:
		manufacturer_name = "NANYA";
		break;
	case HYNIX:
		manufacturer_name = "HYNIX";
		break;
	case MOSEL:
		manufacturer_name = "MOSEL";
		break;
	case WINBOND:
		manufacturer_name = "WINBOND";
		break;
	case ESMT:
		manufacturer_name = "ESMT";
		break;
	case SPANSION:
		manufacturer_name = "SPANSION";
		break;
	case SST:
		manufacturer_name = "SST";
		break;
	case ZMOS:
		manufacturer_name = "ZMOS";
		break;
	case INTEL:
		manufacturer_name = "INTEL";
		break;
	case NUMONYX:
		manufacturer_name = "NUMONYX";
		break;
	case MICRON:
		manufacturer_name = "MICRON";
		break;
	default:
		manufacturer_name = "UNKNOWN";
	}

	switch (vendor_imem_info_parse_dt("qcom,msm-imem-ddr_memory_type")) {
	case DDR_TYPE_LPDDR1:
		device_type = "LPDDR1";
		break;
	case DDR_TYPE_LPDDR2:
		device_type = "LPDDR2";
		break;
	case DDR_TYPE_PCDDR2:
		device_type = "PCDDR3";
		break;
	case DDR_TYPE_PCDDR3:
		device_type = "PCDDR4";
		break;
	case DDR_TYPE_LPDDR3:
		device_type = "LPDDR3";
		break;
	case DDR_TYPE_LPDDR4:
		device_type = "LPDDR4";
		break;
	case DDR_TYPE_LPDDR4X:
		device_type = "LPDDR4X";
		break;
	case DDR_TYPE_LPDDR5:
		device_type = "LPDDR5";
		break;
	case DDR_TYPE_LPDDR5X:
		device_type = "LPDDR5X";
		break;
	default:
		device_type = "UNKNOWN";
	}

	size = vendor_imem_info_parse_dt("qcom,msm-imem-ddr_memory_size");
	size = size / 1024; /* to GB */

	seq_printf(m, "%s-NA-NA-%dGB-%s\n", manufacturer_name, size, device_type);
	return 0;
}

static int board_id_read_proc(struct seq_file *m, void *v)
{
	int id = 0;

#ifdef CONFIG_NUBIA_BOARD_ID
	id = nubia_get_hw_id();
#else
	id = vendor_imem_info_parse_dt("qcom,msm-imem-board-id");
#endif
	seq_printf(m, "%d\n", id);

	return 0;
}

#define VENDOR_IMEM_INFO(field)		\
static int field ## _proc_open(struct inode *inode, struct file *file)	\
{	\
	return single_open(file, field ## _read_proc, NULL);		\
};		\
static const struct proc_ops field ## _proc_fops = {		\
	.proc_open		= field ## _proc_open,		\
	.proc_read		= seq_read,				\
	.proc_lseek		= seq_lseek,		\
	.proc_release	= single_release,		\
};

VENDOR_IMEM_INFO(ddr_id)
VENDOR_IMEM_INFO(board_id)

static void vendor_imem_info_init_procfs(void)
{
	struct proc_dir_entry *pde;

	pde = proc_mkdir("vendor_imem", NULL);
	if (!pde)
		goto err1;

	pde = proc_create("vendor_imem/ddr_id", 0444, NULL, &ddr_id_proc_fops);
	if (!pde)
		goto err2;

	pde = proc_create("vendor_imem/board_id", 0444, NULL, &board_id_proc_fops);
	if (!pde)
		goto err3;

	/* Compatible with former projects */
	pde = proc_create("driver/ddr_id", 0444, NULL, &ddr_id_proc_fops);
	if (!pde)
		goto err4;

	pde = proc_create("driver/board_id", 0444, NULL, &board_id_proc_fops);
	if (!pde)
		goto err5;

	return;

err5:
	remove_proc_entry("driver/ddr_id", NULL);
err4:
	remove_proc_entry("vendor_imem/board_id", NULL);
err3:
	remove_proc_entry("vendor_imem/ddr_id", NULL);
err2:
	remove_proc_entry("vendor_imem", NULL);
err1:
	return;
}

static int __init vendor_imem_info_init(void)
{
	vendor_imem_info_init_procfs();
	return 0;
}

fs_initcall(vendor_imem_info_init);

MODULE_LICENSE("GPL");
