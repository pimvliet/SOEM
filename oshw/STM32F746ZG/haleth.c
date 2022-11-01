#include "haleth.h"

#include "stm32f7xx_hal.h"
#include "lan8742.h"
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

int32_t ETH_PHY_IO_Init(void);
int32_t ETH_PHY_IO_DeInit(void);
int32_t ETH_PHY_IO_ReadReg(uint32_t DevAddr, uint32_t RegAddr,
		uint32_t *pRegVal);
int32_t ETH_PHY_IO_WriteReg(uint32_t DevAddr, uint32_t RegAddr, uint32_t RegVal);
int32_t ETH_PHY_IO_GetTick(void);
int ethernet_link_check_state(void);

lan8742_Object_t LAN8742;
lan8742_IOCtx_t LAN8742_IOCtx =
{ ETH_PHY_IO_Init, ETH_PHY_IO_DeInit, ETH_PHY_IO_WriteReg, ETH_PHY_IO_ReadReg,
		ETH_PHY_IO_GetTick };

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
	heth.Init.RxBuffLen = 1536;

	if (HAL_ETH_Init(&heth) != HAL_OK)
	{
		// ETHERNET SETUP FAILED
		return 0;
	}

	NVIC_DisableIRQ(ETH_IRQn);

	memset(&TxConfig, 0, sizeof(ETH_TxPacketConfig));
	TxConfig.Attributes = ETH_TX_PACKETS_FEATURES_CSUM
			| ETH_TX_PACKETS_FEATURES_CRCPAD;
	TxConfig.ChecksumCtrl = ETH_CHECKSUM_IPHDR_PAYLOAD_INSERT_PHDR_CALC;
	TxConfig.CRCPadCtrl = ETH_CRC_PAD_INSERT;

	LAN8742_RegisterBusIO(&LAN8742, &LAN8742_IOCtx);

	LAN8742_Init(&LAN8742);

	HAL_Delay(500);

	if (ethernet_link_check_state())
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

void Ethernet_Close(void)
{
	HAL_ETH_Stop(&heth);
	HAL_ETH_DeInit(&heth);
}

int Ethernet_Send(unsigned char *data, int len)
{
	// TODO Check link if failure try to open link, if fail return error;
	int ret = 0;

	ret = HAL_ETH_Transmit(&heth, &TxConfig, 1000);

	/*
	 if((heth.TxDesc->Status & ETH_DMATXDESC_OWN) != (uint32_t)RESET)
	 {
	 ret = -1;
	 }
	 */

	return ret;
}

/*******************************************************************************
 PHI IO Functions
 *******************************************************************************/
/**
 * @brief  Initializes the MDIO interface GPIO and clocks.
 * @param  None
 * @retval 0 if OK, -1 if ERROR
 */
int32_t ETH_PHY_IO_Init(void)
{
	/* We assume that MDIO GPIO configuration is already done
	 in the ETH_MspInit() else it should be done here
	 */

	/* Configure the MDIO Clock */
	HAL_ETH_SetMDIOClockRange(&heth);

	return 0;
}

/**
 * @brief  De-Initializes the MDIO interface .
 * @param  None
 * @retval 0 if OK, -1 if ERROR
 */
int32_t ETH_PHY_IO_DeInit(void)
{
	return 0;
}

/**
 * @brief  Read a PHY register through the MDIO interface.
 * @param  DevAddr: PHY port address
 * @param  RegAddr: PHY register address
 * @param  pRegVal: pointer to hold the register value
 * @retval 0 if OK -1 if Error
 */
int32_t ETH_PHY_IO_ReadReg(uint32_t DevAddr, uint32_t RegAddr,
		uint32_t *pRegVal)
{
	if (HAL_ETH_ReadPHYRegister(&heth, DevAddr, RegAddr, pRegVal) != HAL_OK)
	{
		return -1;
	}

	return 0;
}

/**
 * @brief  Write a value to a PHY register through the MDIO interface.
 * @param  DevAddr: PHY port address
 * @param  RegAddr: PHY register address
 * @param  RegVal: Value to be written
 * @retval 0 if OK -1 if Error
 */
int32_t ETH_PHY_IO_WriteReg(uint32_t DevAddr, uint32_t RegAddr, uint32_t RegVal)
{
	if (HAL_ETH_WritePHYRegister(&heth, DevAddr, RegAddr, RegVal) != HAL_OK)
	{
		return -1;
	}

	return 0;
}

/**
 * @brief  Get the time in millisecons used for internal PHY driver process.
 * @retval Time value
 */
int32_t ETH_PHY_IO_GetTick(void)
{
	return HAL_GetTick();
}

/**
 * @brief  Check the ETH link state then update ETH driver and netif link accordingly.
 * @retval None
 */
int ethernet_link_check_state()
{
	ETH_MACConfigTypeDef MACConf =
	{ 0 };
	int32_t PHYLinkState = 0;
	uint32_t linkchanged = 0U, speed = 0U, duplex = 0U;

	PHYLinkState = LAN8742_GetLinkState(&LAN8742);

	if (PHYLinkState <= LAN8742_STATUS_LINK_DOWN)
	{
		HAL_ETH_Stop(&heth);
		return 0;
	}
	else if (PHYLinkState > LAN8742_STATUS_LINK_DOWN)
	{
		switch (PHYLinkState)
		{
		case LAN8742_STATUS_100MBITS_FULLDUPLEX:
			duplex = ETH_FULLDUPLEX_MODE;
			speed = ETH_SPEED_100M;
			linkchanged = 1;
			break;
		case LAN8742_STATUS_100MBITS_HALFDUPLEX:
			duplex = ETH_HALFDUPLEX_MODE;
			speed = ETH_SPEED_100M;
			linkchanged = 1;
			break;
		case LAN8742_STATUS_10MBITS_FULLDUPLEX:
			duplex = ETH_FULLDUPLEX_MODE;
			speed = ETH_SPEED_10M;
			linkchanged = 1;
			break;
		case LAN8742_STATUS_10MBITS_HALFDUPLEX:
			duplex = ETH_HALFDUPLEX_MODE;
			speed = ETH_SPEED_10M;
			linkchanged = 1;
			break;
		default:
			break;
		}

		if (linkchanged)
		{
			/* Get MAC Config MAC */
			HAL_ETH_GetMACConfig(&heth, &MACConf);
			MACConf.DuplexMode = duplex;
			MACConf.Speed = speed;
			HAL_ETH_SetMACConfig(&heth, &MACConf);
			HAL_ETH_Start(&heth);
		}
		PHYLinkState = LAN8742_GetLinkState(&LAN8742);
		if (PHYLinkState >= LAN8742_STATUS_100MBITS_FULLDUPLEX
				&& PHYLinkState <= LAN8742_STATUS_10MBITS_HALFDUPLEX)
		{
			return 1;
		}
	}
	HAL_ETH_Stop(&heth);
	return 0;
}
