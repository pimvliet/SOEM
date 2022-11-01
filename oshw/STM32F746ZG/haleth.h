#ifndef HALETH_H
#define HALETH_H

#include "osal.h"
#include "ethercat.h"

int Ethernet_Init(void);
void Ethernet_Close(void);
int Ethernet_Send(unsigned char *data, int len);

#endif	//HALETH_H
