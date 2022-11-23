#include "w5500_spi.h"

#include "stm32F7xx_hal.h"
#include "wizchip_conf.h"

extern SPI_HandleTypeDef hspi1;

uint16_t m_csPin;
GPIO_TypeDef *m_csPort;

uint16_t m_rstPin;
GPIO_TypeDef *m_rstPort;

SPI_TypeDef *SPI;

void wizchip_select(void)
{
	//m_csPort->BSRR = (uint32_t)m_csPin << 16;
	m_csPort->BSRR = 0x00100000;					// CALCULATED FOR PIN 4
}

void wizchip_deselect(void)
{
	//m_csPort->BSRR = m_csPin;
	m_csPort->BSRR = 0x0010;						// CALCULATED FOR PIN 4
}

uint8_t wizchip_read(void)
{
	while (!(SPI->SR & SPI_FLAG_TXE));

	*(__IO uint8_t*) &SPI->DR = 0x00;

	while (!(SPI->SR & SPI_FLAG_RXNE));

	return (*(__IO uint8_t*) &SPI->DR);
}

void wizchip_write(uint8_t writeByte)
{
	while (!(SPI->SR & SPI_FLAG_TXE));

	*(__IO uint8_t*) &SPI->DR = writeByte;

	while (!(SPI->SR & SPI_FLAG_RXNE));

	(*(__IO uint8_t*) &SPI->DR);
}

void wizchip_readburst(uint8_t *pBuf, uint16_t len)
{
	for (uint16_t i = 0; i < len; i++)
	{
		while (!(SPI->SR & SPI_FLAG_TXE));

		*(__IO uint8_t*) &SPI->DR = 0x00;

		while (!(SPI->SR & SPI_FLAG_RXNE));

		*pBuf =  (*(__IO uint8_t*) &SPI->DR);
		pBuf++;
	}
}

void wizchip_writeburst(uint8_t *pBuf, uint16_t len)
{
	for (uint16_t i = 0; i < len; i++)
	{
		while (!(SPI->SR & SPI_FLAG_TXE));

		*(__IO uint8_t*) &SPI->DR = *pBuf;

		while (!(SPI->SR & SPI_FLAG_RXNE));

		(*(__IO uint8_t*) &SPI->DR);
		pBuf++;
	}
}

void W5500IOInit()
{
	GPIO_InitTypeDef GPIO_InitStruct =
	{ 0 };

	//TODO change to conditional clock enables
	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();

	/*Configure GPIO pin : SPI1_CS1_Pin */
	GPIO_InitStruct.Pin = m_csPin;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
	HAL_GPIO_Init(m_csPort, &GPIO_InitStruct);

	GPIO_InitStruct.Pin = m_rstPin;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
	HAL_GPIO_Init(m_rstPort, &GPIO_InitStruct);
}

int W5500Init(GPIO_TypeDef *csPort, uint16_t csPin, GPIO_TypeDef *rstPort,
		uint16_t rstPin)
{
	uint8_t memsize[2][8] =
	{
	{ 16, 0, 0, 0, 0, 0, 0, 0 },
	{ 16, 0, 0, 0, 0, 0, 0, 0 } };

	m_csPin = csPin;
	m_csPort = csPort;
	m_rstPin = rstPin;
	m_rstPort = rstPort;

	W5500IOInit();

	HAL_GPIO_WritePin(m_csPort, m_csPin, GPIO_PIN_SET);

	HAL_GPIO_WritePin(m_rstPort, m_rstPin, GPIO_PIN_RESET);
	HAL_Delay(10);
	HAL_GPIO_WritePin(m_rstPort, m_rstPin, GPIO_PIN_SET);

	SPI = hspi1.Instance;

	reg_wizchip_cs_cbfunc(wizchip_select, wizchip_deselect);
	reg_wizchip_spi_cbfunc(wizchip_read, wizchip_write);
	reg_wizchip_spiburst_cbfunc(wizchip_readburst, wizchip_writeburst);

	m_csPort->BSRR = (uint32_t) m_csPin << 16;

	if (ctlwizchip(CW_INIT_WIZCHIP, (void*) memsize) == -1)
		return 1;
	else
		return 0;
}
