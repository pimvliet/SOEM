#include "oshw.h"

#include "stdlib.h"

/**
 * Host to Network byte order (i.e. to big endian).
 *
 * Note that Ethercat uses little endian byte order, except for the Ethernet
 * header which is big endian as usual.
 */
uint16 oshw_htons (uint16 hostshort)
{
	uint16_t tmp = 0;
	tmp		=	(hostshort & 0x00ff) << 8;
	tmp		|=	(hostshort & 0xff00) >> 8;
	hostshort	=	tmp;
	return hostshort;
}

/**
 * Network (i.e. big endian) to Host byte order.
 *
 * Note that Ethercat uses little endian byte order, except for the Ethernet
 * header which is big endian as usual.
 */
uint16 oshw_ntohs (uint16 networkshort)
{
	uint16_t tmp = 0;
	tmp		=	(networkshort & 0x00ff) << 8;
	tmp		|=	(networkshort & 0xff00) >> 8;
	networkshort	=	tmp;
	return networkshort;
}

/* Create list over available network adapters.
 * @return First element in linked list of adapters
 */
ec_adaptert* oshw_find_adapters (void)
{
    // Not implemented because only a single adapter is supported
    ec_adaptert * ret_adapter = NULL;
    return ret_adapter;
}

/** Free memory allocated memory used by adapter collection.
 * @param[in] adapter = First element in linked list of adapters
 * EC_NOFRAME.
 */
void oshw_free_adapters (ec_adaptert* adapter)
{
    // Not implemented because only a single adapter is supported
}
