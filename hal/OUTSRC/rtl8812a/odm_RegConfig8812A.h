/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef __INC_ODM_REGCONFIG_H_8812A
#define __INC_ODM_REGCONFIG_H_8812A

#if (RTL8812A_SUPPORT == 1)

void
odm_ConfigRFReg_8812A(
	IN 	PDM_ODM_T 				pDM_Odm,
	IN 	uint32_t 					Addr,
	IN 	uint32_t 					Data,
	IN  ODM_RF_RADIO_PATH_E     RF_PATH,
	IN	uint32_t				    RegAddr
	);
void
odm_ConfigBB_AGC_8812A(
    IN 	PDM_ODM_T 	pDM_Odm,
    IN 	uint32_t 		Addr,
    IN 	uint32_t 		Bitmask,
    IN 	uint32_t 		Data
    );

void
odm_ConfigBB_PHY_REG_PG_8812A(
	IN 	PDM_ODM_T 	pDM_Odm,
    IN 	uint32_t 		Addr,
    IN 	uint32_t 		Bitmask,
    IN 	uint32_t 		Data
    );

void
odm_ConfigBB_PHY_8812A(
	IN 	PDM_ODM_T 	pDM_Odm,
    IN 	uint32_t 		Addr,
    IN 	uint32_t 		Bitmask,
    IN 	uint32_t 		Data
    );

#endif
#endif // end of SUPPORT

