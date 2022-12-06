#ifndef W5500_SPI_H
#define W5500_SPI_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "stdio.h"
#include "stm32f7xx.h"

int W5500Init(GPIO_TypeDef* csPort, uint16_t csPin, GPIO_TypeDef* rstPort, uint16_t rstPin);

#ifdef __cplusplus
}
#endif

#endif
