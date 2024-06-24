#ifndef _ZTE_SHARED_IMEM_H
#define _ZTE_SHARED_IMEM_H

/*==========================================================================
*  Defines :
* ==========================================================================
*/
/** DDR types. */
typedef enum {
	DDR_TYPE_LPDDR1 = 0,           /**< Low power DDR1. */
	DDR_TYPE_LPDDR2 = 2,       /**< Low power DDR2  set to 2 for compatibility*/
	DDR_TYPE_PCDDR2 = 3,           /**< Personal computer DDR2. */
	DDR_TYPE_PCDDR3 = 4,           /**< Personal computer DDR3. */

	DDR_TYPE_LPDDR3 = 5,           /**< Low power DDR3. */
	DDR_TYPE_LPDDR4 = 6,           /**< Low power DDR4. */
	DDR_TYPE_LPDDR4X = 7,          /**< Low power DDR4X. */
	DDR_TYPE_LPDDR5 = 8,           /**< Low power DDR5. */
	DDR_TYPE_LPDDR5X = 9,          /**< Low power DDR5X. */
	DDR_TYPE_UNUSED = 0x7FFFFFFF  /**< For compatibility with deviceprogrammer(features not using DDR). */
} DDR_TYPE;

/** DDR manufacturers. */
typedef enum {
	RESERVED_0,                        /**< Reserved for future use. */
	SAMSUNG,                           /**< Samsung. */
	QIMONDA,                           /**< Qimonda. */
	ELPIDA,                            /**< Elpida Memory, Inc. */
	ETRON,                             /**< Etron Technology, Inc. */
	NANYA,                             /**< Nanya Technology Corporation. */
	HYNIX,                             /**< Hynix Semiconductor Inc. */
	MOSEL,                             /**< Mosel Vitelic Corporation. */
	WINBOND,                           /**< Winbond Electronics Corp. */
	ESMT,                              /**< Elite Semiconductor Memory Technology Inc. */
	RESERVED_1,                        /**< Reserved for future use. */
	SPANSION,                          /**< Spansion Inc. */
	SST,                               /**< Silicon Storage Technology, Inc. */
	ZMOS,                              /**< ZMOS Technology, Inc. */
	INTEL,                             /**< Intel Corporation. */
	NUMONYX = 254,                     /**< Numonyx, acquired by Micron Technology, Inc. */
	MICRON = 255,                      /**< Micron Technology, Inc. */
	DDR_MANUFACTURES_MAX = 0x7FFFFFFF  /**< Forces the enumerator to 32 bits. */
} DDR_MANUFACTURES;

#endif
