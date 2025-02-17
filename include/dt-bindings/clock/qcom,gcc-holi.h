/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DT_BINDINGS_CLK_QCOM_GCC_HOLI_H
#define _DT_BINDINGS_CLK_QCOM_GCC_HOLI_H

#define GPLL0					0
#define GPLL0_OUT_EVEN				1
#define GPLL0_OUT_ODD				2
#define GPLL10					3
#define GPLL10_OUT_EVEN				4
#define GPLL11					5
#define GPLL11_OUT_EVEN				6
#define GPLL11_OUT_ODD				7
#define GPLL3					8
#define GPLL3_OUT_EVEN				9
#define GPLL4					10
#define GPLL4_OUT_EVEN				11
#define GPLL5					12
#define GPLL5_OUT_EVEN				13
#define GPLL6					14
#define GPLL6_OUT_EVEN				15
#define GPLL7					16
#define GPLL7_OUT_EVEN				17
#define GPLL8					18
#define GPLL8_OUT_EVEN				19
#define GPLL9					20
#define GPLL9_OUT_MAIN				21
#define GPLL9_OUT_EARLY				22
#define GCC_AHB2PHY_CSI_CLK			23
#define GCC_AHB2PHY_USB_CLK			24
#define GCC_BIMC_GPU_AXI_CLK			25
#define GCC_BOOT_ROM_AHB_CLK			26
#define GCC_CAM_THROTTLE_NRT_CLK		27
#define GCC_CAM_THROTTLE_RT_CLK			28
#define GCC_CAMERA_AHB_CLK			29
#define GCC_CAMERA_XO_CLK			30
#define GCC_CAMSS_AXI_CLK			31
#define GCC_CAMSS_AXI_CLK_SRC			32
#define GCC_CAMSS_CAMNOC_ATB_CLK		33
#define GCC_CAMSS_CAMNOC_NTS_XO_CLK		34
#define GCC_CAMSS_CCI_0_CLK			35
#define GCC_CAMSS_CCI_0_CLK_SRC			36
#define GCC_CAMSS_CCI_1_CLK                     37
#define GCC_CAMSS_CCI_1_CLK_SRC			38
#define GCC_CAMSS_CPHY_0_CLK			39
#define GCC_CAMSS_CPHY_1_CLK			40
#define GCC_CAMSS_CPHY_2_CLK			41
#define GCC_CAMSS_CPHY_3_CLK			42
#define GCC_CAMSS_CSI0PHYTIMER_CLK		43
#define GCC_CAMSS_CSI0PHYTIMER_CLK_SRC		44
#define GCC_CAMSS_CSI1PHYTIMER_CLK		45
#define GCC_CAMSS_CSI1PHYTIMER_CLK_SRC		46
#define GCC_CAMSS_CSI2PHYTIMER_CLK		47
#define GCC_CAMSS_CSI2PHYTIMER_CLK_SRC		48
#define GCC_CAMSS_MCLK0_CLK			49
#define GCC_CAMSS_MCLK0_CLK_SRC			50
#define GCC_CAMSS_MCLK1_CLK			51
#define GCC_CAMSS_MCLK1_CLK_SRC			52
#define GCC_CAMSS_MCLK2_CLK			53
#define GCC_CAMSS_MCLK2_CLK_SRC			54
#define GCC_CAMSS_MCLK3_CLK			55
#define GCC_CAMSS_MCLK3_CLK_SRC			56
#define GCC_CAMSS_MCLK4_CLK_SRC			57
#define GCC_CAMSS_NRT_AXI_CLK			58
#define GCC_CAMSS_OPE_AHB_CLK			59
#define GCC_CAMSS_OPE_AHB_CLK_SRC		60
#define GCC_CAMSS_OPE_CLK			61
#define GCC_CAMSS_OPE_CLK_SRC			62
#define GCC_CAMSS_RT_AXI_CLK			63
#define GCC_CAMSS_TFE_0_CLK			64
#define GCC_CAMSS_TFE_0_CLK_SRC			65
#define GCC_CAMSS_TFE_0_CPHY_RX_CLK		66
#define GCC_CAMSS_TFE_0_CSID_CLK		67
#define GCC_CAMSS_TFE_0_CSID_CLK_SRC		68
#define GCC_CAMSS_TFE_1_CLK			69
#define GCC_CAMSS_TFE_1_CLK_SRC			70
#define GCC_CAMSS_TFE_1_CPHY_RX_CLK		71
#define GCC_CAMSS_TFE_1_CSID_CLK		72
#define GCC_CAMSS_TFE_1_CSID_CLK_SRC		73
#define GCC_CAMSS_TFE_2_CLK			74
#define GCC_CAMSS_TFE_2_CLK_SRC			75
#define GCC_CAMSS_TFE_2_CPHY_RX_CLK		76
#define GCC_CAMSS_TFE_2_CSID_CLK		77
#define GCC_CAMSS_TFE_2_CSID_CLK_SRC		78
#define GCC_CAMSS_TFE_CPHY_RX_CLK_SRC		79
#define GCC_CAMSS_TOP_AHB_CLK			80
#define GCC_CAMSS_TOP_AHB_CLK_SRC		81
#define GCC_CFG_NOC_USB3_PRIM_AXI_CLK		82
#define GCC_CPUSS_AHB_CLK_SRC			83
#define GCC_CPUSS_AHB_POSTDIV_CLK_SRC		84
#define GCC_CPUSS_GNOC_CLK			85
#define GCC_DISP_AHB_CLK			86
#define GCC_DISP_GPLL0_DIV_CLK_SRC		87
#define GCC_DISP_HF_AXI_CLK			88
#define GCC_DISP_SLEEP_CLK			89
#define GCC_DISP_THROTTLE_CORE_CLK		90
#define GCC_DISP_XO_CLK				91
#define GCC_GP1_CLK				92
#define GCC_GP1_CLK_SRC				93
#define GCC_GP2_CLK				94
#define GCC_GP2_CLK_SRC				95
#define GCC_GP3_CLK				96
#define GCC_GP3_CLK_SRC				97
#define GCC_GPU_CFG_AHB_CLK			98
#define GCC_GPU_GPLL0_CLK_SRC			99
#define GCC_GPU_GPLL0_DIV_CLK_SRC		100
#define GCC_GPU_MEMNOC_GFX_CLK			101
#define GCC_GPU_SNOC_DVM_GFX_CLK		102
#define GCC_GPU_THROTTLE_CORE_CLK		103
#define GCC_GPU_THROTTLE_XO_CLK			104
#define GCC_PDM2_CLK				105
#define GCC_PDM2_CLK_SRC			106
#define GCC_PDM_AHB_CLK				107
#define GCC_PDM_XO4_CLK				108
#define GCC_PRNG_AHB_CLK			109
#define GCC_QMIP_CAMERA_NRT_AHB_CLK		110
#define GCC_QMIP_CAMERA_RT_AHB_CLK		111
#define GCC_QMIP_DISP_AHB_CLK			112
#define GCC_QMIP_GPU_CFG_AHB_CLK		113
#define GCC_QMIP_VIDEO_VCODEC_AHB_CLK		114
#define GCC_QUPV3_WRAP0_CORE_2X_CLK		115
#define GCC_QUPV3_WRAP0_CORE_CLK		116
#define GCC_QUPV3_WRAP0_S0_CLK			117
#define GCC_QUPV3_WRAP0_S0_CLK_SRC		118
#define GCC_QUPV3_WRAP0_S1_CLK			119
#define GCC_QUPV3_WRAP0_S1_CLK_SRC		120
#define GCC_QUPV3_WRAP0_S2_CLK			121
#define GCC_QUPV3_WRAP0_S2_CLK_SRC		122
#define GCC_QUPV3_WRAP0_S3_CLK			123
#define GCC_QUPV3_WRAP0_S3_CLK_SRC		124
#define GCC_QUPV3_WRAP0_S4_CLK			125
#define GCC_QUPV3_WRAP0_S4_CLK_SRC		126
#define GCC_QUPV3_WRAP0_S5_CLK			127
#define GCC_QUPV3_WRAP0_S5_CLK_SRC		128
#define GCC_QUPV3_WRAP1_CORE_2X_CLK		129
#define GCC_QUPV3_WRAP1_CORE_CLK		130
#define GCC_QUPV3_WRAP1_S0_CLK			131
#define GCC_QUPV3_WRAP1_S0_CLK_SRC		132
#define GCC_QUPV3_WRAP1_S1_CLK			133
#define GCC_QUPV3_WRAP1_S1_CLK_SRC		134
#define GCC_QUPV3_WRAP1_S2_CLK			135
#define GCC_QUPV3_WRAP1_S2_CLK_SRC		136
#define GCC_QUPV3_WRAP1_S3_CLK			137
#define GCC_QUPV3_WRAP1_S3_CLK_SRC		138
#define GCC_QUPV3_WRAP1_S4_CLK			139
#define GCC_QUPV3_WRAP1_S4_CLK_SRC		140
#define GCC_QUPV3_WRAP1_S5_CLK			141
#define GCC_QUPV3_WRAP1_S5_CLK_SRC		142
#define GCC_QUPV3_WRAP_0_M_AHB_CLK		143
#define GCC_QUPV3_WRAP_0_S_AHB_CLK		144
#define GCC_QUPV3_WRAP_1_M_AHB_CLK		145
#define GCC_QUPV3_WRAP_1_S_AHB_CLK		146
#define GCC_SDCC1_AHB_CLK			147
#define GCC_SDCC1_APPS_CLK			148
#define GCC_SDCC1_APPS_CLK_SRC			149
#define GCC_SDCC1_ICE_CORE_CLK			150
#define GCC_SDCC1_ICE_CORE_CLK_SRC		151
#define GCC_SDCC2_AHB_CLK			152
#define GCC_SDCC2_APPS_CLK			153
#define GCC_SDCC2_APPS_CLK_SRC			154
#define GCC_SYS_NOC_CPUSS_AHB_CLK		155
#define GCC_SYS_NOC_UFS_PHY_AXI_CLK		156
#define GCC_SYS_NOC_USB3_PRIM_AXI_CLK		157
#define GCC_UFS_PHY_AHB_CLK			158
#define GCC_UFS_PHY_AXI_CLK			159
#define GCC_UFS_PHY_AXI_CLK_SRC			160
#define GCC_UFS_PHY_ICE_CORE_CLK		161
#define GCC_UFS_PHY_ICE_CORE_CLK_SRC		162
#define GCC_UFS_PHY_PHY_AUX_CLK			163
#define GCC_UFS_PHY_PHY_AUX_CLK_SRC		164
#define GCC_UFS_PHY_RX_SYMBOL_0_CLK		165
#define GCC_UFS_PHY_TX_SYMBOL_0_CLK		166
#define GCC_UFS_PHY_UNIPRO_CORE_CLK		167
#define GCC_UFS_PHY_UNIPRO_CORE_CLK_SRC		168
#define GCC_USB30_PRIM_MASTER_CLK		169
#define GCC_USB30_PRIM_MASTER_CLK_SRC		170
#define GCC_USB30_PRIM_MOCK_UTMI_CLK		171
#define GCC_USB30_PRIM_MOCK_UTMI_CLK_SRC	172
#define GCC_USB30_PRIM_MOCK_UTMI_POSTDIV_CLK_SRC  173
#define GCC_USB30_PRIM_SLEEP_CLK		174
#define GCC_USB3_PRIM_CLKREF_CLK		175
#define GCC_USB3_PRIM_PHY_AUX_CLK_SRC		176
#define GCC_USB3_PRIM_PHY_COM_AUX_CLK		177
#define GCC_USB3_PRIM_PHY_PIPE_CLK		178
#define GCC_VCODEC0_AXI_CLK			179
#define GCC_VENUS_AHB_CLK			180
#define GCC_VENUS_CTL_AXI_CLK			181
#define GCC_VIDEO_AHB_CLK			182
#define GCC_VIDEO_AXI0_CLK			183
#define GCC_VIDEO_THROTTLE_CORE_CLK		184
#define GCC_VIDEO_VCODEC0_SYS_CLK		185
#define GCC_VIDEO_VENUS_CLK_SRC			186
#define GCC_VIDEO_VENUS_CTL_CLK			187
#define GCC_VIDEO_XO_CLK			188
#define GCC_UFS_MEM_CLKREF_CLK			189
#define GCC_RX5_PCIE_CLKREF_EN_CLK		190
#define GCC_DISP_GPLL0_CLK_SRC			191
#define GCC_CAMSS_CSI3PHYTIMER_CLK		192
#define GCC_CAMSS_CSI3PHYTIMER_CLK_SRC		193
#define GCC_CAMSS_MCLK4_CLK			194

/* GCC resets */
#define GCC_CAMSS_OPE_BCR			0
#define GCC_CAMSS_TFE_BCR			1
#define GCC_CAMSS_TOP_BCR			2
#define GCC_GPU_BCR				3
#define GCC_MMSS_BCR				4
#define GCC_PDM_BCR				5
#define GCC_PRNG_BCR				6
#define GCC_QUPV3_WRAPPER_0_BCR			7
#define GCC_QUPV3_WRAPPER_1_BCR			8
#define GCC_QUSB2PHY_PRIM_BCR			9
#define GCC_QUSB2PHY_SEC_BCR			10
#define GCC_SDCC1_BCR				11
#define GCC_SDCC2_BCR				12
#define GCC_UFS_PHY_BCR				13
#define GCC_USB30_PRIM_BCR			14
#define GCC_USB_PHY_CFG_AHB2PHY_BCR		15
#define GCC_VCODEC0_BCR				16
#define GCC_VENUS_BCR				17
#define GCC_VIDEO_INTERFACE_BCR			18
#define GCC_USB3_DP_PHY_PRIM_BCR		19
#define GCC_USB3_PHY_PRIM_SP0_BCR		20

#endif
