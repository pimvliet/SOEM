#include "haleth.h"

#include "stm32f7xx_hal.h"
#include "string.h"

#if defined ( __ICCARM__ ) /*!< IAR Compiler */
#pragma location=0x2004c000
ETH_DMADescTypeDef  DMARxDscrTab[ETH_RX_DESC_CNT]; /* Ethernet Rx DMA Descriptors */
#pragma location=0x2004c0a0
ETH_DMADescTypeDef  DMATxDscrTab[ETH_TX_DESC_CNT]; /* Ethernet Tx DMA Descriptors */

#elif defined ( __CC_ARM )  /* MDK ARM Compiler */

__attribute__((at(0x2004c000))) ETH_DMADescTypeDef  DMARxDscrTab[ETH_RX_DESC_CNT]; /* Ethernet Rx DMA Descriptors */
__attribute__((at(0x2004c0a0))) ETH_DMADescTypeDef  DMATxDscrTab[ETH_TX_DESC_CNT]; /* Ethernet Tx DMA Descriptors */

#elif defined ( __GNUC__ ) /* GNU Compiler */
ETH_DMADescTypeDef DMARxDscrTab[ETH_RX_DESC_CNT] __attribute__((section(".RxDecripSection"))); /* Ethernet Rx DMA Descriptors */
ETH_DMADescTypeDef DMATxDscrTab[ETH_TX_DESC_CNT] __attribute__((section(".TxDecripSection"))); /* Ethernet Tx DMA Descriptors */

#endif

ETH_TxPacketConfig TxConfig;
ETH_HandleTypeDef heth;



int Ethernet_Init(void)
{
	static uint8_t MACAddr[6];

	heth.Instance = ETH;
	MACAddr[0] = 0x00;
	MACAddr[1] = 0x80;
	MACAddr[2] = 0xE1;
	MACAddr[3] = 0x00;
	MACAddr[4] = 0x00;
	MACAddr[5] = 0x00;
	heth.Init.MACAddr = &MACAddr[0];
	heth.Init.MediaInterface = HAL_ETH_RMII_MODE;
	heth.Init.TxDesc = DMATxDscrTab;
	heth.Init.RxDesc = DMARxDscrTab;
	heth.Init.RxBuffLen = 1524;

	if (HAL_ETH_Init(&heth) != HAL_OK)
	{
		return 0;
	}

	NVIC_DisableIRQ(ETH_IRQn);

	memset(&TxConfig, 0, sizeof(ETH_TxPacketConfig));
	TxConfig.Attributes = ETH_TX_PACKETS_FEATURES_CSUM
			| ETH_TX_PACKETS_FEATURES_CRCPAD;
	TxConfig.ChecksumCtrl = ETH_CHECKSUM_IPHDR_PAYLOAD_INSERT_PHDR_CALC;
	TxConfig.CRCPadCtrl = ETH_CRC_PAD_INSERT;

	HAL_ETH_Start(&heth);

	/*
	uint32_t bcr = 0;
	if (HAL_ETH_ReadPHYRegister(&heth, DP83848_PHY_ADDRESS, PHY_BCR, &bcr) != HAL_OK)
	{
		return 0;
	}
	//disable autoneg
	bcr &= PHY_AUTONEGOTIATION;

	//100mbit full duplex
	bcr |= PHY_FULLDUPLEX_100M;

	//reset phy
	bcr |= PHY_RESTART_AUTONEGOTIATION;

	if(HAL_ETH_WritePHYRegister(&heth, DP83848_PHY_ADDRESS, PHY_BCR, bcr) != HAL_OK)
	{
		return 0;
	}

	HAL_Delay(PHY_CONFIG_DELAY);
	*/

	uint32_t status = 0;
	for (int i = 0; i < 20; i++)
	{
		if (HAL_ETH_ReadPHYRegister(&heth, DP83848_PHY_ADDRESS, PHY_BSR, &status) == HAL_OK)
		{
			if(status & PHY_LINKED_STATUS)
			{
				// Successful link connection
				return 1;
			}
		}
		HAL_Delay(100);
	}

	return 0;
}

void Ethernet_Close(void)
{
	HAL_ETH_Stop(&heth);
}
