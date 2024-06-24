#ifndef _AW9620X_H_
#define _AW9620X_H_

#include <linux/regmap.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/hrtimer.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/leds.h>
#include <linux/regulator/consumer.h>

#define BASE_ADDR			(0x1A00)

//AFE MAP
#define REG_SCANCTRL0			((0x0000) + BASE_ADDR)
/* registers list */
//A2022_96208_0806_AFE MAP
#define REG_SCANCTRL1			((0x0004) + BASE_ADDR)
#define REG_SCANCTRL2			((0x0008) + BASE_ADDR)
#define REG_SCANCTRL3			((0x000C) + BASE_ADDR)
#define REG_AFECFG0_CH0			((0x0044) + BASE_ADDR)
#define REG_AFECFG1_CH0			((0x0048) + BASE_ADDR)
#define REG_AFECFG2_CH0			((0x004C) + BASE_ADDR)
#define REG_AFECFG3_CH0			((0x0050) + BASE_ADDR)
#define REG_AFECFG0_CH1			((0x0098) + BASE_ADDR)
#define REG_AFECFG1_CH1			((0x009C) + BASE_ADDR)
#define REG_AFECFG2_CH1			((0x00A0) + BASE_ADDR)
#define REG_AFECFG3_CH1			((0x00A4) + BASE_ADDR)
#define REG_AFECFG0_CH2			((0x00EC) + BASE_ADDR)
#define REG_AFECFG1_CH2			((0x00F0) + BASE_ADDR)
#define REG_AFECFG2_CH2			((0x00F4) + BASE_ADDR)
#define REG_AFECFG3_CH2			((0x00F8) + BASE_ADDR)
#define REG_AFECFG0_CH3			((0x0140) + BASE_ADDR)
#define REG_AFECFG1_CH3			((0x0144) + BASE_ADDR)
#define REG_AFECFG2_CH3			((0x0148) + BASE_ADDR)
#define REG_AFECFG3_CH3			((0x014C) + BASE_ADDR)
#define REG_AFECFG0_CH4			((0x0194) + BASE_ADDR)
#define REG_AFECFG1_CH4			((0x0198) + BASE_ADDR)
#define REG_AFECFG2_CH4			((0x019C) + BASE_ADDR)
#define REG_AFECFG3_CH4			((0x01A0) + BASE_ADDR)
#define REG_AFECFG0_CH5			((0x01E8) + BASE_ADDR)
#define REG_AFECFG1_CH5			((0x01EC) + BASE_ADDR)
#define REG_AFECFG2_CH5			((0x01F0) + BASE_ADDR)
#define REG_AFECFG3_CH5			((0x01F4) + BASE_ADDR)
#define REG_AFECFG0_CH6			((0x023C) + BASE_ADDR)
#define REG_AFECFG1_CH6			((0x0240) + BASE_ADDR)
#define REG_AFECFG2_CH6			((0x0244) + BASE_ADDR)
#define REG_AFECFG3_CH6			((0x0248) + BASE_ADDR)
#define REG_AFECFG0_CH7			((0x0290) + BASE_ADDR)
#define REG_AFECFG1_CH7			((0x0294) + BASE_ADDR)
#define REG_AFECFG2_CH7			((0x0298) + BASE_ADDR)
#define REG_AFECFG3_CH7			((0x029C) + BASE_ADDR)

/* registers list */
//A2022_96208_0806_DSP MAP
#define REG_FILTCTRL_CH0		((0x0054) + BASE_ADDR)
#define REG_REFERENCE_CH0		((0x0058) + BASE_ADDR)
#define REG_BLFILT_CH0			((0x005C) + BASE_ADDR)
#define REG_PROXCTRL_CH0		((0x0060) + BASE_ADDR)
#define REG_PROXTH0_CH0			((0x0064) + BASE_ADDR)
#define REG_PROXTH1_CH0			((0x0068) + BASE_ADDR)
#define REG_INITPROX0_CH0		((0x006C) + BASE_ADDR)
#define REG_INITPROX1_CH0		((0x0070) + BASE_ADDR)
#define REG_FILTCTRL_CH1		((0x00A8) + BASE_ADDR)
#define REG_REFERENCE_CH1		((0x00AC) + BASE_ADDR)
#define REG_BLFILT_CH1			((0x00B0) + BASE_ADDR)
#define REG_PROXCTRL_CH1		((0x00B4) + BASE_ADDR)
#define REG_PROXTH0_CH1			((0x00B8) + BASE_ADDR)
#define REG_PROXTH1_CH1			((0x00BC) + BASE_ADDR)
#define REG_INITPROX0_CH1		((0x00C0) + BASE_ADDR)
#define REG_INITPROX1_CH1		((0x00C4) + BASE_ADDR)
#define REG_FILTCTRL_CH2		((0x00FC) + BASE_ADDR)
#define REG_REFERENCE_CH2		((0x0100) + BASE_ADDR)
#define REG_BLFILT_CH2			((0x0104) + BASE_ADDR)
#define REG_PROXCTRL_CH2		((0x0108) + BASE_ADDR)
#define REG_PROXTH0_CH2			((0x010C) + BASE_ADDR)
#define REG_PROXTH1_CH2			((0x0110) + BASE_ADDR)
#define REG_INITPROX0_CH2		((0x0114) + BASE_ADDR)
#define REG_INITPROX1_CH2		((0x0118) + BASE_ADDR)
#define REG_FILTCTRL_CH3		((0x0150) + BASE_ADDR)
#define REG_REFERENCE_CH3		((0x0154) + BASE_ADDR)
#define REG_BLFILT_CH3			((0x0158) + BASE_ADDR)
#define REG_PROXCTRL_CH3		((0x015C) + BASE_ADDR)
#define REG_PROXTH0_CH3			((0x0160) + BASE_ADDR)
#define REG_PROXTH1_CH3			((0x0164) + BASE_ADDR)
#define REG_INITPROX0_CH3		((0x0168) + BASE_ADDR)
#define REG_INITPROX1_CH3		((0x016C) + BASE_ADDR)
#define REG_FILTCTRL_CH4		((0x01A4) + BASE_ADDR)
#define REG_REFERENCE_CH4		((0x01A8) + BASE_ADDR)
#define REG_BLFILT_CH4			((0x01AC) + BASE_ADDR)
#define REG_PROXCTRL_CH4		((0x01B0) + BASE_ADDR)
#define REG_PROXTH0_CH4			((0x01B4) + BASE_ADDR)
#define REG_PROXTH1_CH4			((0x01B8) + BASE_ADDR)
#define REG_INITPROX0_CH4		((0x01BC) + BASE_ADDR)
#define REG_INITPROX1_CH4		((0x01C0) + BASE_ADDR)
#define REG_FILTCTRL_CH5		((0x01F8) + BASE_ADDR)
#define REG_REFERENCE_CH5		((0x01FC) + BASE_ADDR)
#define REG_BLFILT_CH5			((0x0200) + BASE_ADDR)
#define REG_PROXCTRL_CH5		((0x0204) + BASE_ADDR)
#define REG_PROXTH0_CH5			((0x0208) + BASE_ADDR)
#define REG_PROXTH1_CH5			((0x020C) + BASE_ADDR)
#define REG_INITPROX0_CH5		((0x0210) + BASE_ADDR)
#define REG_INITPROX1_CH5		((0x0214) + BASE_ADDR)
#define REG_FILTCTRL_CH6		((0x024C) + BASE_ADDR)
#define REG_REFERENCE_CH6		((0x0250) + BASE_ADDR)
#define REG_BLFILT_CH6			((0x0254) + BASE_ADDR)
#define REG_PROXCTRL_CH6		((0x0258) + BASE_ADDR)
#define REG_PROXTH0_CH6			((0x025C) + BASE_ADDR)
#define REG_PROXTH1_CH6			((0x0260) + BASE_ADDR)
#define REG_INITPROX0_CH6		((0x0264) + BASE_ADDR)
#define REG_INITPROX1_CH6		((0x0268) + BASE_ADDR)
#define REG_FILTCTRL_CH7		((0x02A0) + BASE_ADDR)
#define REG_REFERENCE_CH7		((0x02A4) + BASE_ADDR)
#define REG_BLFILT_CH7			((0x02A8) + BASE_ADDR)
#define REG_PROXCTRL_CH7		((0x02AC) + BASE_ADDR)
#define REG_PROXTH0_CH7			((0x02B0) + BASE_ADDR)
#define REG_PROXTH1_CH7			((0x02B4) + BASE_ADDR)
#define REG_INITPROX0_CH7		((0x02B8) + BASE_ADDR)
#define REG_INITPROX1_CH7		((0x02BC) + BASE_ADDR)


/* registers list */
//A2022_96208_0806_STAT MAP
#define REG_FWVER			((0x0010) + BASE_ADDR)
#define REG_WST				((0x0014) + BASE_ADDR)
#define REG_STAT0			((0x0018) + BASE_ADDR)
#define REG_STAT1			((0x001C) + BASE_ADDR)

/* registers list */
//A2022_96208_0806_DATA MAP
#define REG_CHINTEN			((0x0020) + BASE_ADDR)

#define REG_RAW_CH0			((0X0074) + BASE_ADDR)
#define REG_LPFDATA_CH0			((0X0078) + BASE_ADDR)
#define REG_VALID_CH0			((0X007C) + BASE_ADDR)
#define REG_BASELINE_CH0		((0X0080) + BASE_ADDR)
#define REG_DIFF_CH0			((0X0084) + BASE_ADDR)
#define REG_DELTAVALID_CH0		((0x0088) + BASE_ADDR)
#define REG_INITVALUE_CH0		((0x008C) + BASE_ADDR)
#define REG_RAW_CH1			((0x00C8) + BASE_ADDR)
#define REG_LPFDATA_CH1			((0x00CC) + BASE_ADDR)
#define REG_VALID_CH1			((0x00D0) + BASE_ADDR)
#define REG_BASELINE_CH1		((0x00D4) + BASE_ADDR)
#define REG_DIFF_CH1			((0x00D8) + BASE_ADDR)
#define REG_DELTAVALID_CH1		((0x00DC) + BASE_ADDR)
#define REG_INITVALUE_CH1		((0x00E0) + BASE_ADDR)
#define REG_RAW_CH2			((0x011C) + BASE_ADDR)
#define REG_LPFDATA_CH2			((0x0120) + BASE_ADDR)
#define REG_VALID_CH2			((0x0124) + BASE_ADDR)
#define REG_BASELINE_CH2		((0x0128) + BASE_ADDR)
#define REG_DIFF_CH2			((0x012C) + BASE_ADDR)
#define REG_DELTAVALID_CH2		((0x0130) + BASE_ADDR)
#define REG_INITVALUE_CH2		((0x0134) + BASE_ADDR)
#define REG_RAW_CH3			((0x0170) + BASE_ADDR)
#define REG_LPFDATA_CH3			((0x0174) + BASE_ADDR)
#define REG_VALID_CH3			((0x0178) + BASE_ADDR)
#define REG_BASELINE_CH3		((0x017C) + BASE_ADDR)
#define REG_DIFF_CH3			((0x0180) + BASE_ADDR)
#define REG_DELTAVALID_CH3		((0x0184) + BASE_ADDR)
#define REG_INITVALUE_CH3		((0x0188) + BASE_ADDR)
#define REG_RAW_CH4			((0x01C4) + BASE_ADDR)
#define REG_LPFDATA_CH4			((0x01C8) + BASE_ADDR)
#define REG_VALID_CH4			((0x01CC) + BASE_ADDR)
#define REG_BASELINE_CH4		((0x01D0) + BASE_ADDR)
#define REG_DIFF_CH4			((0x01D4) + BASE_ADDR)
#define REG_DELTAVALID_CH4		((0x01D8) + BASE_ADDR)
#define REG_INITVALUE_CH4		((0x01DC) + BASE_ADDR)
#define REG_RAW_CH5			((0x0218) + BASE_ADDR)
#define REG_LPFDATA_CH5			((0x021C) + BASE_ADDR)
#define REG_VALID_CH5			((0x0220) + BASE_ADDR)
#define REG_BASELINE_CH5		((0x0224) + BASE_ADDR)
#define REG_DIFF_CH5			((0x0228) + BASE_ADDR)
#define REG_DELTAVALID_CH5		((0x022C) + BASE_ADDR)
#define REG_INITVALUE_CH5		((0x0230) + BASE_ADDR)
#define REG_RAW_CH6			((0x026C) + BASE_ADDR)
#define REG_LPFDATA_CH6			((0x0270) + BASE_ADDR)
#define REG_VALID_CH6			((0x0274) + BASE_ADDR)
#define REG_BASELINE_CH6		((0x0278) + BASE_ADDR)
#define REG_DIFF_CH6			((0x027C) + BASE_ADDR)
#define REG_DELTAVALID_CH6		((0x0280) + BASE_ADDR)
#define REG_INITVALUE_CH6		((0x0284) + BASE_ADDR)
#define REG_RAW_CH7			((0x02C0) + BASE_ADDR)
#define REG_LPFDATA_CH7			((0x02C4) + BASE_ADDR)
#define REG_VALID_CH7			((0x02C8) + BASE_ADDR)
#define REG_BASELINE_CH7		((0x02CC) + BASE_ADDR)
#define REG_DIFF_CH7			((0x02D0) + BASE_ADDR)
#define REG_DELTAVALID_CH7		((0x02D4) + BASE_ADDR)
#define REG_SENSY_CONFIG		(0x1d20)
#define REG_SENSY_CONFIG_VAL_LOW	(0x0000000D)
#define REG_SENSY_CONFIG_VAL_MED	(0x0000000C)
#define REG_SENSY_CONFIG_VAL_HIGH	(0x0000000B)
#define REG_INITVALUE_CH7		((0x02D8) + BASE_ADDR)
#define REG_TEMPERATURE			((0X0024) + BASE_ADDR)
#define REG_TEMPEINITVALUE		((0X0028) + BASE_ADDR)
/* registers list */
//A2022_96208_0806_SFR MAP
#define REG_GPIOWDATA			(0x4000)
#define REG_GPIODIR			(0x4004)
#define REG_GPIORDATA			(0x4008)
#define REG_GPIOIBE			(0x400c)
#define REG_GPIOPU			(0x4010)
#define REG_GPIOPD			(0x4014)
#define REG_GPIOINTEN			(0x4020)
#define REG_GPIOINTTRIG			(0x4024)
#define REG_GPIOINTCLR			(0x4028)
#define REG_GPIOCTRL			(0x4040)
#define REG_GPIOMFP			(0x4904)
#define REG_CMD				(0x4408)
#define REG_I2CINTEN			(0x440C)
#define REG_HOSTIRQSRC			(0x4410)
#define REG_HOSTIRQEN			(0x4414)
#define REG_MISC2HOSTIRQTRIG		(0x4418)
#define REG_MISC2HOSTIRQEN		(0x441C)
#define REG_I2CADDR			(0x4440)
#define REG_MCFG00			(0x4444)
#define REG_ANACFG00			(0x4448)
#define REG_RAMBIST_IN00		(0x4490)
#define REG_RAMBIST_OUT00		(0x4494)
#define REG_BOOT_LOADER_ACTIVE		(0x4748)
#define REG_CHIP_ID			(0xFF00)
#define REG_CHIPSTAT			(0xFF04)
#define REG_HOSTCTRL1			(0xFF10)
#define REG_HOSTCTRL2			(0xFF14)
#define REG_RSTNALL			(0xFF18)
#define REG_APB_ACCESS_EN		(0xFF20)
#define REG_PASSWD			(0xFF80)
#define REG_ATESTIN00			(0xFF84)
#define REG_SCANMODE			(0xFF88)
#define REG_TESTMODE			(0xFF8C)

#define REG_PMU_CFG			(0x4820)
#define REG_SET_PMU_CFG			(0x6)
#define REG_ENSET_PMU_CFG		(0x4)

#define REG_BTROM_EW_EN			(0x4794)
#define REG_SET_BTROM_EW_EN		(0x5a637955)
#define REG_ENSET_BTROM_EW_EN		(0x0)

#define REG_ISP_ADDR			(0x4704)
#define REG_ISP_CMD			(0x4710)
#define REG_ACCESS_MAIN_ARR		(0x05)
#define REG_ISP_CMD_MAIN_ARR		(0X03)
#define REG_ISP_CMD_CONFIG		(0x0c)

#define REG_T_RCV			(0x472c)
#define REG_SET_T_RCV			(0xf0)
#define REG_ISP_GO			(0x4714)
#define REG_SET_ISP_GO			(0x1)

#define REG_ISP_RDDATA			(0x470c)

#define REG_SET_T_RCV_EN		(0x16)

#define REG_OPEN_REG_ACCESS_EN		(0x3c00ffff)
#define REG_OPEN_MCFG_EN		(0x10000)
#define REG_OPEN_APB_ACCESS_EN		(0x3c00f091)

#define REG_SET_MCFG00			(0x0)

#define REG_CHECKSUM_W_ADDR		(0x3ffc)
#define AW_FW_CHECK_LEN			(0x37f8)

#define REG_SET_BOOT_LOADER_ACTIVE	(0x1)

#define AW_FW_W_DATA_ADDR		(0x4708)
#define AW_FW_EN_POSITTION		(0x37f8)
#define AW_BT_EN_POSITTION		(0x3ff8)
#define AW_BT_BIN_LEN			(0x7f8)

#define AW_REG_MCFG00_RST		(1 << 16)
#define AW_REG_MCFG00_RST_MASK		(~(1 << 16))
#define AW_REG_MCFG00_NO_RST		(0 << 16)

#define AW_REG_FLASH_WAKE_UP		(0x4700)

#define REG_MCFG00_CPU_HALT		(0x00010000)
#define REG_APB_ACCESS_EN_ACCESS	(0x3CFFFFFF)
#define REG_BOOT_LOADER_ACTIVE_CLOSE	(0x00000000)
#define NVR3_PROTECT			(0x4788)
#define NVR3_PROTECT_OPEN		(0x5A637922)
#define ISP_CR_OPEN			(0x110)
#define REG_T_ERASE			(0x4734)
#define REG_T_ERASE_OPEN		(0x4650)
#define REG_ISP_ADDR_W_VAL		(0x4100)
#define REG_ISP_CMD_W_VAL		(0x25)
#define AW_FW_W_DATA_ADDR_STA		(0x0)
#define REG_ISP_CMD_W_VAL_EN		(0x2C)
#define REG_ISP_GO_W_VAL		(0x1)
#define REG_MCFG00_EN			(0x00000000)
#define REG_RSTNALL_EN			(0x3c)

#define AW_BT_HOST2CPU_TRIG_ONLINE_UPGRADE_CMD		(0x14)
#define AW_BT_STATE_INFO_ERR		(0x1c00)
#define AW_BT_STATE_INFO_PST		(0x1C04)
#define AW_BT_STATE_INFO_FLAG		(0x1C08)
#define AW_BT_PROT_CMD_PACK_ADDR	(0x1C10)
#define AW_BT_PROT_ACK_PACK_ADDR	(0x1C14)
#define AW_BT_MESSAGE_MSG0		(0x1C20)
#define AW_BT_MESSAGE_MSG1		(0x1C24)
#define AW_BT_MESSAGE_MSG_FLAG		(0x1C28)
#define	AW_BT_VER_INF_VERSION		(0x1C30)
#define	AW_BT_VER_INF_DATE		(0x1C34)
#define	AW_BT_HOST2CPU_TRIG		(0x4408)
#define AW_BTCPU2HOST_TRIG		(0x4410)
#define AW_WAIT_IRQ_CYCLES				(10)
#define AW_WAIT_ENTER_SLEEP_MODE		(100)
#define AW_WAIT_ENTER_TR_MODE			(100)
#define AW_ACK_ADDR				(0x1800)
#define INIT_OVER_IRQ_OK			(1)
#define AW_REG_WST_MODE_SLEEP			(0x03000000)
#define AW_PROT_STOP_WAIT_IRQ_CYCLES		(100)
#define AW_SRAM_FIRST_DETECT		(0x20000800)
#define AW_CACHE_LEN			(0x1000)
#define AW_PROT_HEAD_MAX_SZIE		(0x40)
#define REG_UPDATA_DIS			(0x4744)
#define AW_REG_UPDATA_DIS_MASK		(0x00ffffff)
#define REG_ACCESSEN_CLOSE		(0x3c00f011)
#define AW_EN_TR_CHECK_VALUE_LEN		(16)
#define AW_FW_CHECKSUM_EN_ADDR		(0x10003FF8)
#define AW_PUB_VER_REG_WST		(0x1a10)

#define AW_FLASH_ERASE_SECTOR_START_ADDR	(0x10002000)
#define AW_SECTOR_SIZE				(128)
#define AW_ERASE_SECTOR_CNT			(64 - 1)


enum AW_UPDATE_FW_STATE {
	SEND_UPDTAE_CMD,
	SEND_START_CMD,
	SEND_ERASE_SECTOR_CMD,
	SEND_UPDATE_FW_CMD,
	SEND_UPDATE_CHECK_CODE_CMD,
	SEND_RESTORE_CMD,
	SEND_STOP_CMD,
};
enum AW_PROT_UPDATE_FW_DELAY_TIME{
	SEND_UPDTAE_CMD_DELAY_MS = 1,
	SEND_START_CMD_DELAY_MS = 3,
	SEND_ERASE_SECTOR_CMD_DELAY_MS = 5,
	SEND_UPDATE_FW_CMD_DELAY_MS = 100,
	SEND_UPDATE_CHECK_CODE_CMD_DELAY_MS = 1,
	SEND_RESTORE_CMD_DELAY_MS = 27,
	SEND_STOP_CMD_DELAY_MS = 25,
};
enum AW_PROT_RECV_LEN {
	SEND_START_CMD_RECV_LEN = 8,
	SEND_ERASE_CHIP_CMD_RECV_LEN = 4,
	SEND_UPDATE_FW_CMD_RECV_LEN = 8,
	SEND_UPDATE_CHECK_CODE_CMD_RECV_LEN = 4,
	SEND_RESTORE_CMD_RECV_LEN = 4,
};
struct aw_reg_data {
	unsigned char rw;
	unsigned short reg;
};
/********************************************
 * Register Access
 *******************************************/
#define REG_RD_ACCESS					(1 << 0)
#define REG_WR_ACCESS					(1 << 1)

static const struct aw_reg_data aw9620x_reg_access[] = {
	//AFE MAP
	{ .reg = REG_SCANCTRL0,			.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_SCANCTRL1,			.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_SCANCTRL2, .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_SCANCTRL3, .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_AFECFG0_CH0,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_AFECFG1_CH0,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_AFECFG2_CH0, .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_AFECFG3_CH0,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_AFECFG0_CH1,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_AFECFG1_CH1,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_AFECFG2_CH1, .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_AFECFG3_CH1,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_AFECFG0_CH2,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_AFECFG1_CH2,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_AFECFG2_CH2, .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_AFECFG3_CH2,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_AFECFG0_CH3,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_AFECFG1_CH3,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_AFECFG2_CH3, .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_AFECFG3_CH3,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_AFECFG0_CH4,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_AFECFG1_CH4,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_AFECFG2_CH4, .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_AFECFG3_CH4,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_AFECFG0_CH5,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_AFECFG1_CH5,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_AFECFG2_CH5, .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_AFECFG3_CH5,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_AFECFG0_CH6,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_AFECFG1_CH6,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_AFECFG2_CH6, .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_AFECFG3_CH6,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_AFECFG0_CH7,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_AFECFG1_CH7,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_AFECFG2_CH7, .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_AFECFG3_CH7,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },

	//DSP MAP
	{ .reg = REG_FILTCTRL_CH0,	.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_REFERENCE_CH0,	.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_BLFILT_CH0,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_PROXCTRL_CH0,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_PROXTH0_CH0,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_PROXTH1_CH0,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_INITPROX0_CH0,	.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_INITPROX1_CH0,	.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_FILTCTRL_CH1,	.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_REFERENCE_CH1,	.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_BLFILT_CH1,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_PROXCTRL_CH1,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_PROXTH0_CH1,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_PROXTH1_CH1,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_INITPROX0_CH1,	.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_INITPROX1_CH1,	.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_FILTCTRL_CH2,	.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_REFERENCE_CH2,	.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_BLFILT_CH2,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_PROXCTRL_CH2,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_PROXTH0_CH2,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_PROXTH1_CH2,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_INITPROX0_CH2,	.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_INITPROX1_CH2,	.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_FILTCTRL_CH3,	.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_REFERENCE_CH3,	.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_BLFILT_CH3,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_PROXCTRL_CH3,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_PROXTH0_CH3,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_PROXTH1_CH3,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_INITPROX0_CH3,	.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_INITPROX1_CH3,	.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_FILTCTRL_CH4,	.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_REFERENCE_CH4,	.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_BLFILT_CH4,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_PROXCTRL_CH4,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_PROXTH0_CH4,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_PROXTH1_CH4,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_INITPROX0_CH4,	.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_INITPROX1_CH4,	.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_FILTCTRL_CH5,	.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_REFERENCE_CH5,	.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_BLFILT_CH5,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_PROXCTRL_CH5,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_PROXTH0_CH5,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_PROXTH1_CH5,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_INITPROX0_CH5,	.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_INITPROX1_CH5,	.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_FILTCTRL_CH6,	.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_REFERENCE_CH6,	.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_BLFILT_CH6,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_PROXCTRL_CH6,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_PROXTH0_CH6,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_PROXTH1_CH6,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_INITPROX0_CH6,	.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_INITPROX1_CH6,	.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_FILTCTRL_CH7,	.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_REFERENCE_CH7,	.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_BLFILT_CH7,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_PROXCTRL_CH7,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_PROXTH0_CH7,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_PROXTH1_CH7,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_INITPROX0_CH7,	.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_INITPROX1_CH7,	.rw = REG_RD_ACCESS | REG_WR_ACCESS, },

	//STAT MAP
	{ .reg = REG_FWVER,			.rw = REG_RD_ACCESS, },
	{ .reg = REG_WST,			.rw = REG_RD_ACCESS, },
	{ .reg = REG_STAT0,			.rw = REG_RD_ACCESS, },
	{ .reg = REG_STAT1,		.rw = REG_RD_ACCESS, },
	{ .reg = REG_CHINTEN,		.rw = REG_RD_ACCESS | REG_WR_ACCESS, },

	//DATA MAP
	{ .reg = REG_RAW_CH0,		 .rw = REG_RD_ACCESS, },
	{ .reg = REG_LPFDATA_CH0,	 .rw = REG_RD_ACCESS, },
	{ .reg = REG_VALID_CH0,		 .rw = REG_RD_ACCESS, },
	{ .reg = REG_BASELINE_CH0,		.rw = REG_RD_ACCESS, },
	{ .reg = REG_DIFF_CH0,			.rw = REG_RD_ACCESS, },
	{ .reg = REG_DELTAVALID_CH0, 	 .rw = REG_RD_ACCESS, },
	{ .reg = REG_INITVALUE_CH0,	 .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_RAW_CH1,		 .rw = REG_RD_ACCESS, },
	{ .reg = REG_LPFDATA_CH1,	 .rw = REG_RD_ACCESS, },
	{ .reg = REG_VALID_CH1,		 .rw = REG_RD_ACCESS, },
	{ .reg = REG_BASELINE_CH1,		.rw = REG_RD_ACCESS, },
	{ .reg = REG_DIFF_CH1,			.rw = REG_RD_ACCESS, },
	{ .reg = REG_DELTAVALID_CH1, 	 .rw = REG_RD_ACCESS, },
	{ .reg = REG_INITVALUE_CH1,	 .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_RAW_CH2,		 .rw = REG_RD_ACCESS, },
	{ .reg = REG_LPFDATA_CH2,	 .rw = REG_RD_ACCESS, },
	{ .reg = REG_VALID_CH2,		 .rw = REG_RD_ACCESS, },
	{ .reg = REG_BASELINE_CH2,		.rw = REG_RD_ACCESS, },
	{ .reg = REG_DIFF_CH2,			.rw = REG_RD_ACCESS, },
	{ .reg = REG_DELTAVALID_CH2, 	 .rw = REG_RD_ACCESS, },
	{ .reg = REG_INITVALUE_CH2,	 .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_RAW_CH3,		 .rw = REG_RD_ACCESS, },
	{ .reg = REG_LPFDATA_CH3,	 .rw = REG_RD_ACCESS, },
	{ .reg = REG_VALID_CH3,		 .rw = REG_RD_ACCESS, },
	{ .reg = REG_BASELINE_CH3,		.rw = REG_RD_ACCESS, },
	{ .reg = REG_DIFF_CH3,			.rw = REG_RD_ACCESS, },
	{ .reg = REG_DELTAVALID_CH3, 	 .rw = REG_RD_ACCESS, },
	{ .reg = REG_INITVALUE_CH3,	 .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_RAW_CH4,		 .rw = REG_RD_ACCESS, },
	{ .reg = REG_LPFDATA_CH4,	 .rw = REG_RD_ACCESS, },
	{ .reg = REG_VALID_CH4,		 .rw = REG_RD_ACCESS, },
	{ .reg = REG_BASELINE_CH4,		.rw = REG_RD_ACCESS, },
	{ .reg = REG_DIFF_CH4,			.rw = REG_RD_ACCESS, },
	{ .reg = REG_DELTAVALID_CH4, 	 .rw = REG_RD_ACCESS, },
	{ .reg = REG_INITVALUE_CH4,	 .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_RAW_CH5,		 .rw = REG_RD_ACCESS, },
	{ .reg = REG_LPFDATA_CH5,	 .rw = REG_RD_ACCESS, },
	{ .reg = REG_VALID_CH5,		 .rw = REG_RD_ACCESS, },
	{ .reg = REG_BASELINE_CH5,		.rw = REG_RD_ACCESS, },
	{ .reg = REG_DIFF_CH5,			.rw = REG_RD_ACCESS, },
	{ .reg = REG_DELTAVALID_CH5, 	 .rw = REG_RD_ACCESS, },
	{ .reg = REG_INITVALUE_CH5,	 .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_RAW_CH6,		 .rw = REG_RD_ACCESS, },
	{ .reg = REG_LPFDATA_CH6,	 .rw = REG_RD_ACCESS, },
	{ .reg = REG_VALID_CH6,		 .rw = REG_RD_ACCESS, },
	{ .reg = REG_BASELINE_CH6,		.rw = REG_RD_ACCESS, },
	{ .reg = REG_DIFF_CH6,			.rw = REG_RD_ACCESS, },
	{ .reg = REG_DELTAVALID_CH6, 	 .rw = REG_RD_ACCESS, },
	{ .reg = REG_INITVALUE_CH6,	 .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_RAW_CH7,		 .rw = REG_RD_ACCESS, },
	{ .reg = REG_LPFDATA_CH7,	 .rw = REG_RD_ACCESS, },
	{ .reg = REG_VALID_CH7,		 .rw = REG_RD_ACCESS, },
	{ .reg = REG_BASELINE_CH7,		.rw = REG_RD_ACCESS, },
	{ .reg = REG_DIFF_CH7,			.rw = REG_RD_ACCESS, },
	{ .reg = REG_DELTAVALID_CH7, 	 .rw = REG_RD_ACCESS, },
	{ .reg = REG_INITVALUE_CH7,	 .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_TEMPERATURE,	 .rw = REG_RD_ACCESS, },
	{ .reg = REG_TEMPEINITVALUE,	 .rw = REG_RD_ACCESS | REG_WR_ACCESS, },

	//SFR MAP
	{ .reg = REG_GPIOWDATA,		 .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_GPIODIR,		 .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_GPIORDATA,		 .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_GPIOIBE,		 .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_GPIOPU,		 .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_GPIOPD,		 .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_GPIOINTEN,		 .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_GPIOINTTRIG,	 .rw = REG_RD_ACCESS, },
	{ .reg = REG_GPIOINTCLR,	 .rw = REG_WR_ACCESS, },
	{ .reg = REG_GPIOCTRL,		 .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_GPIOMFP,		 .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_CMD,		 .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_I2CINTEN,		 .rw = REG_RD_ACCESS, },
	{ .reg = REG_HOSTIRQSRC,	 .rw = REG_RD_ACCESS, },
	{ .reg = REG_HOSTIRQEN,		 .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_MISC2HOSTIRQTRIG,   .rw = REG_RD_ACCESS, },
	{ .reg = REG_MISC2HOSTIRQEN,	 .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_I2CADDR,		 .rw = REG_RD_ACCESS, },
	{ .reg = REG_MCFG00,		 .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_ANACFG00,		 .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_RAMBIST_IN00,	 .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_RAMBIST_OUT00,	 .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_BOOT_LOADER_ACTIVE, .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_CHIP_ID,			.rw = REG_RD_ACCESS, },
	{ .reg = REG_CHIPSTAT,			.rw = REG_RD_ACCESS, },
 	{ .reg = REG_HOSTCTRL1,		 .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
 	{ .reg = REG_HOSTCTRL2,		 .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
	{ .reg = REG_RSTNALL,			.rw = REG_RD_ACCESS | REG_WR_ACCESS, },
 	{ .reg = REG_APB_ACCESS_EN,	 .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
 	{ .reg = REG_PASSWD,		 .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
 	{ .reg = REG_ATESTIN00,		 .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
 	{ .reg = REG_SCANMODE,		 .rw = REG_WR_ACCESS, },
 	{ .reg = REG_TESTMODE,		 .rw = REG_RD_ACCESS | REG_WR_ACCESS, },
};

//aw96208 reg config
static const uint32_t aw9620x_reg_default[] = {
	0x1a00, 0x0000ffff,
	0x1a04, 0x00ff0032,
	0x1a08, 0x0017a13f,
	0x1a0c, 0x00000000,
	0x1a44, 0x09500001,
	0x1a48, 0x282a0000,
	0x1a4c, 0xd81c0207,
	0x1a50, 0x00000000,
	0x1a98, 0x09500004,
	0x1a9c, 0x26370000,
	0x1aa0, 0xd81c0207,
	0x1aa4, 0x00000000,
	0x1aec, 0x09500010,
	0x1af0, 0x21270000,
	0x1af4, 0xd81c0207,
	0x1af8, 0x00000000,
	0x1b40, 0x09500040,
	0x1b44, 0x22320000,
	0x1b48, 0xd81c0207,
	0x1b4c, 0x00000000,
	0x1b94, 0x09500100,
	0x1b98, 0x24090000,
	0x1b9c, 0xd81c0207,
	0x1ba0, 0x00000000,
	0x1be8, 0x09500400,
	0x1bec, 0x201b0000,
	0x1bf0, 0xd81c0207,
	0x1bf4, 0x00000000,
	0x1c3c, 0x09501000,
	0x1c40, 0x24340000,
	0x1c44, 0xd81c0207,
	0x1c48, 0x00000000,
	0x1c90, 0x09504000,
	0x1c94, 0x20270000,
	0x1c98, 0xd81c0207,
	0x1c9c, 0x00000000,
	0x1a54, 0x08000000,
	0x1a58, 0x00000000,
	0x1a5c, 0xff0005a0,
	0x1a60, 0x00000000,
	0x1a64, 0x00030d40,
	0x1a68, 0x00061a80,
	0x1a6c, 0x00000000,
	0x1a70, 0x00000000,
	0x1aa8, 0x08000000,
	0x1aac, 0x00000000,
	0x1ab0, 0xff0005a0,
	0x1ab4, 0x00000000,
	0x1ab8, 0x00030d40,
	0x1abc, 0x00061a80,
	0x1ac0, 0x00000000,
	0x1ac4, 0x00000000,
	0x1afc, 0x08000000,
	0x1b00, 0x00000000,
	0x1b04, 0xff0005a0,
	0x1b08, 0x00000000,
	0x1b0c, 0x00030d40,
	0x1b10, 0x00061a80,
	0x1b14, 0x00000000,
	0x1b18, 0x00000000,
	0x1b50, 0x08000000,
	0x1b54, 0x00000000,
	0x1b58, 0xff0005a0,
	0x1b5c, 0x00000000,
	0x1b60, 0x00030d40,
	0x1b64, 0x00061a80,
	0x1b68, 0x00000000,
	0x1b6c, 0x00000000,
	0x1ba4, 0x08000000,
	0x1ba8, 0x00000000,
	0x1bac, 0xff0005a0,
	0x1bb0, 0x00000000,
	0x1bb4, 0x00030d40,
	0x1bb8, 0x00061a80,
	0x1bbc, 0x00000000,
	0x1bc0, 0x00000000,
	0x1bf8, 0x08000000,
	0x1bfc, 0x00000000,
	0x1c00, 0xff0005a0,
	0x1c04, 0x00000000,
	0x1c08, 0x00030d40,
	0x1c0c, 0x00061a80,
	0x1c10, 0x00000000,
	0x1c14, 0x00000000,
	0x1c4c, 0x08000000,
	0x1c50, 0x00000000,
	0x1c54, 0xff0005a0,
	0x1c58, 0x00000000,
	0x1c5c, 0x00030d40,
	0x1c60, 0x00061a80,
	0x1c64, 0x00000000,
	0x1c68, 0x00000000,
	0x1ca0, 0x08000000,
	0x1ca4, 0x00000000,
	0x1ca8, 0xff0005a0,
	0x1cac, 0x00000000,
	0x1cb0, 0x00030d40,
	0x1cb4, 0x00061a80,
	0x1cb8, 0x00000000,
	0x1cbc, 0x00000000,
	0x4414, 0x0000000f,
	0x441c, 0x00000000,
	0x1a20, 0xffffffff,
	0x1a8c, 0x000b3000,
	0x1ae0, 0xffd9b000,
	0x1b34, 0x00414000,
	0x1b88, 0x009bc000,
	0x1bdc, 0x00461000,
	0x1c30, 0x00fdf000,
	0x1c84, 0x00724000,
	0x1cd8, 0x0048d000,
	0x1a28, 0x00000000,
};

#define NULL    ((void *)0)
#define GET_32_DATA(w, x, y, z) ((unsigned int)(((w) << 24) | ((x) << 16) | ((y) << 8) | (z)))
#define BIN_NUM_MAX   100
#define HEADER_LEN    60
/*********************************************************
 *
 * header information
 *
 ********************************************************/
enum bin_header_version_enum {
	HEADER_VERSION_1_0_0 = 0x01000000,
};

enum data_type_enum {
	DATA_TYPE_REGISTER = 0x00000000,
	DATA_TYPE_DSP_REG = 0x00000010,
	DATA_TYPE_DSP_CFG = 0x00000011,
	DATA_TYPE_SOC_REG = 0x00000020,
	DATA_TYPE_SOC_APP = 0x00000021,
	DATA_TYPE_MULTI_BINS = 0x00002000,
};

enum data_version_enum {
	DATA_VERSION_V1 = 0X00000001,	/*default little edian */
	DATA_VERSION_MAX,
};

struct bin_header_info {
	unsigned int header_len; /* Frame header length */
	unsigned int check_sum; /* Frame header information-Checksum */
	unsigned int header_ver; /* Frame header information-Frame header version */
	unsigned int bin_data_type; /* Frame header information-Data type */
	unsigned int bin_data_ver; /* Frame header information-Data version */
	unsigned int bin_data_len; /* Frame header information-Data length */
	unsigned int ui_ver; /* Frame header information-ui version */
	unsigned char chip_type[20]; /* Frame header information-chip type */
	unsigned int reg_byte_len; /* Frame header information-reg byte len */
	unsigned int data_byte_len; /* Frame header information-data byte len */
	unsigned int device_addr; /* Frame header information-device addr */
	unsigned int valid_data_len; /* Length of valid data obtained after parsing */
	unsigned int valid_data_addr; /* The offset address of the valid data obtained after parsing relative to info */

	unsigned int reg_num; /* The number of registers obtained after parsing */
	unsigned int reg_data_byte_len; /* The byte length of the register obtained after parsing */
	unsigned int download_addr; /* The starting address or download address obtained after parsing */
	unsigned int app_version; /* The software version number obtained after parsing */
};

/************************************************************
*
* function define
*
************************************************************/
struct bin_container {
	unsigned int len; /* The size of the bin file obtained from the firmware */
	unsigned char data[]; /* Store the bin file obtained from the firmware */
};

struct aw_bin {
	char *p_addr; /* Offset pointer (backward offset pointer to obtain frame header information and important information) */
	unsigned int all_bin_parse_num; /* The number of all bin files */
	unsigned int multi_bin_parse_num; /* The number of single bin files */
	unsigned int single_bin_parse_num; /* The number of multiple bin files */
	struct bin_header_info header_info[BIN_NUM_MAX]; /* Frame header information and other important data obtained after parsing */
	struct bin_container info; /* Obtained bin file data that needs to be parsed */
};

extern int aw_parsing_bin_file(struct aw_bin *bin);
int aw_parse_bin_header_1_0_0(struct aw_bin *bin);

#define AWLOGD(dev, format, arg...) \
	do {\
		 dev_printk(KERN_DEBUG, dev, \
			"[%s:%d] "format"\n", __func__, __LINE__, ##arg);\
	} while (0)

#define AWLOGI(dev, format, arg...) \
	do {\
		dev_printk(KERN_INFO, dev, \
			"[%s:%d] "format"\n", __func__, __LINE__, ##arg);\
	} while (0)

#define AWLOGE(dev, format, arg...) \
	do {\
		 dev_printk(KERN_ERR, dev, \
			"[%s:%d] "format"\n", __func__, __LINE__, ##arg);\
	} while (0)

#define AW_GET_U32_DATA(a, b, c, d) (((uint32_t)(a) << (24)) | ((uint32_t)(b) << (16)) | ((uint32_t)(c) << (8)) | ((uint32_t)(d)))
#define AW_GET_U16_DATA(a, b) 	(((uint32_t)(a) << (8)) | ((uint32_t)(b) << (0)))

#define AW_USE_IRQ_FLAG

#define AW_VCC_MIN_UV				(1700000)
#define AW_VCC_MAX_UV				(3600000)
#define AW9620X_CHIP_MIN_VOLTAGE		(1600000)
#define AW_RETRIES				(5)
#define AW_POWER_ON_DELAY_MS			(25)
#define AW_IRQ_DELAY_MS				(100)
#define AW_POWER_ON_SYSFS_DELAY_MS		(5000)

#define REG_RSTNALL_VAL				(0x3c)

#define TRANSFER_SEQ_LEN			(4)
#define TRANSFER_DTS_ADDR_LEN			(4)

#define LEN_BYTE_2				(2)
#define LEN_BYTE_4				(4)
#define INIT_OVER_IRQ				(1)
#define INIT_OVER_IRQ_OK			(1)
#define INIT_OVER_IRQ_ERR			(0)
#define SOFT_RST_OK				(1)
#define UPDATE_FRIMWARE_OK			(1)

#define AW9620X_AWRW_OffSET			(20)
#define AW9620X_AWRW_DATA_WIDTH			(5)
#define AW_DATA_OffSET_2			(2)
#define AW_DATA_OffSET_3			(3)


#define AW_CHANNEL_NUM_MAX			(8)




/********aw9620x_reg_mode_update start********/

#define AW_CHECK_EN_VAL			(0x20222022)

#define AW_BT_CHECK_LEN			(0x7f8)
#define AW_BT_CHECK_EN_ADDR		(0x07F8)
#define AW_BT_CHECK_CODE_ADDR		(0x07FC)

#define AW_FW_CHECK_LEN			(0x37f8)
#define AW_FW_CHECK_EN_ADDR		(0x3FF8)
#define AW_FW_CHECK_CODE_ADDR		(0x3FFC)

#define AW_BT_TR_START_ADDR		(0x0000)

#define AW_BT_CHECK_START_ADDR		(0x0000)

#define AW_FW_TR_START_ADDR		(0x2000)

#define AW_FW_CHECK_START_ADDR		(0x0800)

#define SOCTOR_SIZE			(128)

#define AW_FINE_ADJUST_STEP0		(165)
#define AW_COARSE_ADJUST_STEP0		(9900)
#define AW_FINE_ADJUST_STEP1		(330)
#define AW_COARSE_ADJUST_STEP1		(19800)

#define AW_PARA_TIMES			(10000)

enum AW9620X_UPDATE_MODE {
	BOOT_UPDATE,
	FRIMWARE_UPDATE,
};

enum AW9620X_BOOT_VER {
	AW_OLD_BT,
	AW_NEW_BT,
};

struct check_info {
	uint32_t check_len;
	uint32_t flash_check_start_addr;
	uint32_t w_check_en_addr;
	uint32_t w_check_code_addr;
};

struct aw_update_common {
	uint8_t update_flag;
	uint8_t *w_bin_offset;
	uint32_t update_data_len;
	uint32_t flash_tr_start_addr;
	struct check_info *check_info;
};

/********aw9620x_reg_mode_update END********/

/*****************aw_type start******************/

#ifndef AW_TURE
#define AW_TURE					(1)
#endif

#ifndef AW_FALSE
#define AW_FALSE				(0)
#endif

#ifndef OFFSET_BIT_0
#define OFFSET_BIT_0                            (0)
#endif

#ifndef OFFSET_BIT_8
#define OFFSET_BIT_8				(8)
#endif

#ifndef OFFSET_BIT_16
#define OFFSET_BIT_16				(16)
#endif

#ifndef OFFSET_BIT_24
#define OFFSET_BIT_24				(24)
#endif

#ifndef WORD_LEN
#define WORD_LEN				(4)
#endif

#ifndef GET_BITS_7_0
#define GET_BITS_7_0				(0x00FF)
#endif

#ifndef GET_BITS_15_8
#define GET_BITS_15_8				(0xFF00)
#endif

#ifndef GET_BITS_24_16
#define GET_BITS_24_16				(0x00FF0000)
#endif

#ifndef GET_BITS_31_25
#define GET_BITS_31_25				(0xFF000000)
#endif

#define AW_GET_32_DATA(w, x, y, z)              ((unsigned int)(((w) << 24) | ((x) << 16) | ((y) << 8) | (z)))
#define AW_GET_16_DATA(a, b) 	                (((uint32_t)(a) << (8)) | ((uint32_t)(b) << (0)))

#define AW_REVERSEBYTERS_U16(value) ((((value) & (0x00FF)) << (8)) | (((value) & (0xFF00)) >> (8)))
#define AW_REVERSEBYTERS_U32(value) ((((value) & (0x000000FF)) << (24)) | ((((value) & (0x0000FF00))) << (8)) | (((value) & (0x00FF0000)) >> (8)) | (((value) & (0xFF000000)) >> (24)))
/*****************aw_type end********************/

/************************************************/
struct aw_fw_load_info{
	uint32_t fw_len;
	uint8_t *p_fw_data;
	uint32_t app_version;
	uint8_t direct_update_flag;
};

#define PROT_SEND_ADDR	(0x0800)
#define	SEND_ADDR_LEN	(2)

#define AW_PACK_FIXED_SIZE 			(12)
#define AW_ADDR_SIZE 				(2)

//hecksum_en host->slave
#define AW_HEADER_VAL				(0x02)

#define REG_H2C_TRIG_PARSE_CMD			(0x15)

#define WAIT_IRQ_CYCLES				(100)
#define AW_ACK_ADDR				(0x1800)
#define INIT_OVER_IRQ_OK			(1)
#define AW_TRANSFER_CMD_ACK_VAL_LEN		(8)

#define TRANSFER_SEQ_LEN			(4)
#define TRANSFER_DTS_ADDR_LEN			(4)

//#define SOCTOR_SIZE				(128)
//#define AW_MAIN_STRART_FW_ADDR		(0x0000)
//#define AW_MAIN_END_FW_ADDR			(0x37FF)

//#define AW_MAIN_STRART_BT_ADDR		(0x3800)
//#define AW_MAIN_END_BT_ADDR			(0x3FFF)

#define AW_BT_CHECK_SUM_ADDR			(0x07fc)
#define AW_START_CMD_ACK_VAL_LEN		(8)
#define AW_START_CMD_ACK_STATUS			(2)
#define AW_START_CMD_ACK_ADDR_LEN		(2)

#define P_AW_START_CMD_SEND_VALUE		(NULL)
#define AW_START_CMD_SEND_VALUE_LEN		(0)

#define P_AW_ERASE_CHIP_CMD_SEND_VALUE		(NULL)
#define AW_ERASE_CHIP_CMD_SEND_VALUE_LEN	(0)

#define P_AW_RESTORE_CMD_SEND_VALUE		(NULL)
#define AW_RESTORE_CMD_SEND_VALUE_LEN		(0)

#define P_AW_STOP_CMD_SEND_VALUE		(NULL)
#define AW_STOP_CMD_SEND_VALUE_LEN		(12)

#define AW_TRANSFER_CMD_ACK_VAL_LEN		(8)

#define AW_ERASE_CHIP_CMD_ACK_VAL_LEN		(4)

#define AW_RESTORE_CMD_ACK_VAL_LEN		(4)
#define AW_STOP_CMD_ACK_VAL_LEN			(4)

#define	AW_FLASH_ERASE_CHIO_CMD_ACK_LEN		(4)
#define AW_FLASH_HEAD_ADDR			(0x10002000)

#define AW_REG_MCFG				(0x4444)
#define AW_REG_ACESS_EN				(0xff20)
#define AW_REG_BOOTLOADER_ACTIVER		(0x4748)
#define AW_REG_MCFG				(0x4444)
#define AW_REG_RSTNALL				(0xff18)

#define AW_CPU_HALT				(0x00010000)
#define AW_ACC_PERI				(0x3c00ffff)
#define AWDIS_HARD_BT_MODE			(0x00000000)
#define AW_CPU_RUN				(0x00000000)
#define AW_RSTNALL				(0x0000003c)

#define AW_SCANCTR_AOTEN_MASK		(~(0xff << 8))
#define AW_SCANCTR_AOTEN_EN		(0xff << 8)
enum AW9620X_FLASH_ADDR {
	FLASH_ADDR_BOOT,
	FLASH_ADDR_FW,
};

enum AW9620X_CMP_VAL {
	BIN_VER_GREATER_THAN_SOC_VER,
	BIN_VER_NO_GREATER_THAN_SOC_VER,
};

enum AW_MODULE {
	UPDATE_MODULE = 0x01,
	FLASH_MODULE,
	QUERY_MODULE,
};

enum AW_UPDATE_COMMAND {
	UPDATE_START_CMD = 0X01,
	UPDATE_TRANSFER_CMD,
	UPDATE_STOP_CMD,
	UPDATE_RESTORE_FLASHBT_CMD,
};

enum AW_UPDATE_COMMAND_ACK {
	UPDATE_START_CMD_ACK = 0X01,
	UPDATE_RANSFER_CMD_ACK,
	UPDATE_STOP_CMD_ACK,
	UPDATE_RESTORE_FLASHBT_CMD_ACK,
};

enum AW_FLASH_COMMAND {
	FLASH_ERASE_CHIP_CMD = 0X01,
	FLASH_ERASE_SECTOR_CMD,
};

enum AW_FLASH_COMMAND_ACK {
	FLASH_ERASE_CHIP_CMD_ACK = 0X01,
	FLASH_ERASE_SECTOR_CMD_ACK,
};

enum AW_QUERY_COMMAND {
	QUERY_BT_VER_CMD = 0X01,
	QUERY_BT_DATE_CMD,
	QUERY_ERR_CODE_CMD,
	QUERY_PST_CMD,
	QUERY_CACHE_CMD,
};

enum AW_QUERY_COMMAND_ACK {
	QUERY_BT_VER_CMD_ACK = 0X01,
	QUERY_BT_DATE_CMD_ACK,
	QUERY_ERR_CODE_CMD_ACK,
	QUERY_PST_CMD_ACK,
	QUERY_CACHE_CMD_ACK,
};

struct aw_soc_protocol {
	uint16_t header;
	uint16_t size;
	uint32_t checksum;
	uint8_t module;
	uint8_t command;
	uint16_t length;
	uint8_t value[0];
};
/************************************************/

enum aw9620x_err_code {
	AW_OK,
	AW_ERR,
	AW_ERR_CHIPID,
	AW_ERR_IRQ_INIT,
	AW_PROT_UPDATE_ERR,
};

enum aw_i2c_flags {
	I2C_WR,
	I2C_RD,
};

enum AW9620X_CHIP_ID_VALUE {
	AW96203CSR_CHIP_ID = 0x20226203,
	AW96205DNR_CHIP_ID = 0x20226205,
	AW96208CSR_CHIP_ID = 0x20226208,
};

enum AW9620X_OPERAION_MODE {
	AW9620X_ACTIVE_MODE = 1,
	AW9620X_SLEEP_MODE,
	AW9620X_DEEPSLEEP_MODE,
};

enum AW9620X_UPDATE_FW_MODE {
	AGREEMENT_UPDATE_FW,
	REG_UPDATE_FW,
};

enum AW9620X_HOST_IRQ_STAT {
	IRQ_ENABLE,
	IRQ_DISABLE,
};

enum aw9620x_irq_trigger_position {
	TRIGGER_FAR,
	TRIGGER_TH0,
	TRIGGER_TH1 = 0x03,
};

enum AW9620X_SENSITIVITY_CONFIG	{
	SENSY_LOW = 1,
	SENSY_MEDIUM,
	SENSY_HIGH,
};
struct aw_channels_info {
	uint8_t used;
	uint32_t last_channel_info;
	struct input_dev *input;
	uint8_t name[30];
};

struct aw_awrw_info {
	uint8_t rw_flag;
	uint8_t addr_len;
	uint8_t data_len;
	uint8_t reg_num;
	uint32_t i2c_tranfar_data_len;
	uint8_t *p_i2c_tranfar_data;
};

struct aw_bt_and_fw_info {
	uint8_t update_flag;
	uint32_t fw_bin_version;
	uint32_t fw_version;
	uint32_t fw_checksum;
	uint32_t bt_version;
	uint32_t bt_date;
	uint32_t bt_checksum;
	uint32_t bt_bin_version;
};
struct aw9620x {
	bool arr_update_flag;
	uint8_t power_enable;
	uint8_t updata_flag;
	uint8_t update_fw_mode;
	uint8_t use_defaule_reg_arr_flag;

	uint8_t host_irq_stat;
	uint8_t init_err_flag;
	uint8_t direct_updata_flag;
	uint32_t sar_num;
	int32_t irq_gpio;
	uint32_t old_mode;
	uint32_t hostirqen;
	uint32_t channel_use_flag;
	int32_t to_irq;
	uint32_t old_bt_flag;
	uint32_t prot_update_fw_flag;

	struct i2c_client *i2c;
	struct device *dev;
	struct led_classdev cdev;
	
	struct regulator *vcc;
	struct aw_channels_info *channels_arr;

	struct delayed_work update_work;

	struct aw_awrw_info awrw_info;
	struct aw_bt_and_fw_info bt_fw_info;
	uint8_t cur_sen;
	uint8_t chip_name[30];
	uint8_t fw_name[30];
	uint8_t bt_name[30];
	uint8_t reg_name[30];


	struct pinctrl *ts_pinctrl; 
	struct pinctrl_state *pinctrl_state_active; 
	struct pinctrl_state *pinctrl_state_suspend;	
	struct pinctrl_state *pinctrl_state_default;

};



#endif

