/** \file
 *
 *  Header file for SDCardManager.c.
 */
 
#ifndef _SD_MANAGER_H
#define _SD_MANAGER_H

	/* Includes: */
		#include <avr/io.h>
		
		#include "MassStorage.h"
		#include "Descriptors.h"

		#include <LUFA/Common/Common.h>
		#include <LUFA/Drivers/USB/USB.h>
		#include <LUFA/Drivers/Peripheral/Serial.h>

	/* Defines: */
		/** Block size of the device. This is kept at 512 to remain compatible with the OS despite the underlying
		 *  storage media (Dataflash) using a different native block size. Do not change this value.
		 */
		#define VIRTUAL_MEMORY_BLOCK_SIZE           512

		//these need to be the same as in the 328 firmware
		//TODO: move to a single file included by both
		#define SD_ERROR 0
		#define SD_READ 1
		#define SD_WRITE 2
		#define SD_GET_INFO 3
		#define SD_SUCCESS 4
		#define SD_SET_ADDR 5
		#define SD_SET_BLKS 6
		#define SD_ABORT 7

	/* Function Prototypes: */
		void SDCardManager_Init(void);
		uint32_t SDCardManager_GetNbBlocks(void);
		void SDCardManager_WriteBlocks(const uint32_t BlockAddress, uint16_t TotalBlocks);
		void SDCardManager_ReadBlocks(uint32_t BlockAddress, uint16_t TotalBlocks);
		void SDCardManager_WriteBlocks_RAM(const uint32_t BlockAddress, uint16_t TotalBlocks,
		                                      uint8_t* BufferPtr) ATTR_NON_NULL_PTR_ARG(3);
		void SDCardManagerManager_ReadBlocks_RAM(const uint32_t BlockAddress, uint16_t TotalBlocks,
		                                     uint8_t* BufferPtr) ATTR_NON_NULL_PTR_ARG(3);
		void SDCardManager_ResetDataflashProtections(void);
		bool SDCardManager_CheckDataflashOperation(void);
		
#endif
