#include "w5500_spi.h"

#include "stm32F7xx_hal.h"
#include "wizchip_conf.h"

extern SPI_HandleTypeDef hspi1;

uint16_t m_csPin;
GPIO_TypeDef* m_csPort;

uint16_t m_rstPin;
GPIO_TypeDef* m_rstPort;

uint8_t SPIReadWrite(uint8_t data)
{
	//wait until FIFO has a free slot
	SPI_TypeDef* SPI = hspi1.Instance;
	while (!(SPI->SR & SPI_FLAG_TXE));

	*(__IO uint8_t*) &SPI->DR = data;

	while (!(SPI->SR & SPI_FLAG_RXNE));

	return (*(__IO uint8_t*) &SPI->DR);
}

void wizchip_select(void)
{
	HAL_GPIO_WritePin(m_csPort, m_csPin, GPIO_PIN_RESET);
}

void wizchip_deselect(void)
{
	HAL_GPIO_WritePin(m_csPort, m_csPin, GPIO_PIN_SET);
}

uint8_t wizchip_read(void)
{
	uint8_t readBit;
	readBit = SPIReadWrite(0x00);
	return readBit;
}

void wizchip_write(uint8_t writeBit)
{
	SPIReadWrite(writeBit);
}

void wizchip_readburst(uint8_t *pBuf, uint16_t len)
{
	for (uint16_t i = 0; i < len; i++)
	{
		*pBuf = SPIReadWrite(0x00);
		pBuf++;
	}
}

void wizchip_writeburst(uint8_t *pBuf, uint16_t len)
{
	for (uint16_t i = 0; i < len; i++)
	{
		SPIReadWrite(*pBuf);
		pBuf++;
	}
}

void W5500IOInit()
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

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

void W5500Init(GPIO_TypeDef* csPort, uint16_t csPin, GPIO_TypeDef* rstPort, uint16_t rstPin)
{
	uint8_t tmp;
	uint8_t memsize[2][8] = {{16, 0, 0, 0, 0, 0, 0, 0}, {16, 0, 0, 0, 0, 0, 0, 0}};

	m_csPin = csPin;
	m_csPort = csPort;
	m_rstPin = rstPin;
	m_rstPort = rstPort;

	W5500IOInit();


	HAL_GPIO_WritePin(m_csPort, m_csPin, GPIO_PIN_SET);

	HAL_GPIO_WritePin(m_rstPort, m_rstPin, GPIO_PIN_RESET);
	HAL_Delay(10);
	HAL_GPIO_WritePin(m_rstPort, m_rstPin, GPIO_PIN_SET);

	reg_wizchip_cs_cbfunc(wizchip_select, wizchip_deselect);
	reg_wizchip_spi_cbfunc(wizchip_read, wizchip_write);
	reg_wizchip_spiburst_cbfunc(wizchip_readburst, wizchip_writeburst);
#if 1
	if (ctlwizchip(CW_INIT_WIZCHIP, (void*)memsize) == -1)
	{
		printf("WIZCHIP Initialize fail.\r\n");
		while(1);																	//TODO remove infinite loop
	}
#endif
	printf("WIZCHIP Initialize success.\r\n");
}
