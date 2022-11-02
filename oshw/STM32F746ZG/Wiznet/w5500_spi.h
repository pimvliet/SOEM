#ifndef W5500_SPI_H
#define W5500_SPI_H

#include "stdio.h"
#include "stm32f7xx.h"

void W5500Init(GPIO_TypeDef* csPort, uint16_t csPin, GPIO_TypeDef* rstPort, uint16_t rstPin);

#endif
