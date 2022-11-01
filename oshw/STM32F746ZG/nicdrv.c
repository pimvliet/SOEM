/*
 * Licensed under the GNU General Public License version 2 with exceptions. See
 * LICENSE file in the project root for full license information
 */

/** \file
 * \brief
 * EtherCAT RAW socket driver.
 *
 * Low level interface functions to send and receive EtherCAT packets.
 * EtherCAT has the property that packets are only send by the master,
 * and the send packets always return in the receive buffer.
 * There can be multiple packets "on the wire" before they return.
 * To combine the received packets with the original send packets a buffer
 * system is installed. The identifier is put in the index item of the
 * EtherCAT header. The index is stored and compared when a frame is received.
 * If there is a match the packet can be combined with the transmit packet
 * and returned to the higher level function.
 *
 * The socket layer can exhibit a reversal in the packet order (rare).
 * If the Tx order is A-B-C the return order could be A-C-B. The indexed buffer
 * will reorder the packets automatically.
 *
 * The "redundant" option will configure two sockets and two NIC interfaces.
 * Slaves are connected to both interfaces, one on the IN port and one on the
 * OUT port. Packets are send via both interfaces. Any one of the connections
 * (also an interconnect) can be removed and the slaves are still serviced with
 * packets. The software layer will detect the possible failure modes and
 * compensate. If needed the packets from interface A are resent through interface B.
 * This layer is fully transparent for the higher layers.
 */
#include <string.h>

#include "nicdrv.h"
#include "osal.h"
#include "haleth.h"

// host byte order to network byte order
static uint16_t htons(uint16_t data)
{
	uint16_t temp = 0;
	temp = (data & 0x00ff) << 8;
	temp |= (data & 0xff00) >> 8;
	data = temp;
	return data;
}

// network byte order to host byte order
static uint16_t ntohs(uint16_t data)
{
	uint16_t temp = 0;
	temp = (data & 0x00ff) << 8;
	temp |= (data & 0xff00) >> 8;
	data = temp;
	return data;
}

/** Redundancy modes */
enum
{
	/** No redundancy, single NIC mode */
	ECT_RED_NONE,
	/** Double redundant NIC connection */
	ECT_RED_DOUBLE
};

/** Primary source MAC address used for EtherCAT.
 * This address is not the MAC address used from the NIC.
 * EtherCAT does not care about MAC addressing, but it is used here to
 * differentiate the route the packet traverses through the EtherCAT
 * segment. This is needed to fund out the packet flow in redundant
 * configurations. */
const uint16 priMAC[3] = { 0x0101, 0x0101, 0x0101 };
/** Secondary source MAC address used for EtherCAT. */
const uint16 secMAC[3] = { 0x0404, 0x0404, 0x0404 };

/** second MAC word is used for identification */
#define RX_PRIM priMAC[1]
/** second MAC word is used for identification */
#define RX_SEC secMAC[1]

static void ecx_clear_rxbufstat(int *rxbufstat)
{
	int i;
	for (i = 0; i < EC_MAXBUF; i++)
	{
		rxbufstat[i] = EC_BUF_EMPTY;
	}
}

/** Basic setup to connect NIC to socket.
 * @param[in] port        = port context struct
 * @param[in] ifname       = Name of NIC device, f.e. "eth0"
 * @param[in] secondary      = NOT SUPPORTED ATM if >0 then use secondary stack instead of primary
 * @return >0 if succeeded
 */
int ecx_setupnic(ecx_portt *port, const char *ifname, int secondary)
{
	int i;
	int *psock;

	if (secondary)
	{
		// secondary port struct available?
		if (port->redport)
		{
			// when using secondary socket it is automatically a redundant setup
			psock = &(port->redport->sockhandle);
			*psock = -1;
			port->redstate = ECT_RED_DOUBLE;
			port->redport->stack.sock = &(port->redport->sockhandle);
			port->redport->stack.txbuf = &(port->txbuf);
			port->redport->stack.txbuflength = &(port->txbuflength);
			port->redport->stack.tempbuf = &(port->redport->tempinbuf);
			port->redport->stack.rxbuf = &(port->redport->rxbuf);
			port->redport->stack.rxbufstat = &(port->redport->rxbufstat);
			port->redport->stack.rxsa = &(port->redport->rxsa);
			ecx_clear_rxbufstat(&(port->redport->rxbufstat[0]));
		}
		else
		{
			// redport not available
			return 0;
		}
	}
	else
	{
		port->sockhandle = 0;
		port->lastidx = 0;
		port->redstate = ECT_RED_NONE;
		port->stack.sock = &(port->sockhandle);
		port->stack.txbuf = &(port->txbuf);
		port->stack.txbuflength = &(port->txbuflength);
		port->stack.tempbuf = &(port->tempinbuf);
		port->stack.rxbuf = &(port->rxbuf);
		port->stack.rxbufstat = &(port->rxbufstat);
		port->stack.rxsa = &(port->rxsa);
		ecx_clear_rxbufstat(&(port->rxbufstat[0]));
		psock = &(port->sockhandle);
	}

	//int res = Ethernet_Init();
	int res = 0;

	if (!res)
	{
		// Interface was not able to open
		return 0;
	}

	*psock = 0;

	for (i = 0; i < EC_MAXBUF; i++)
	{
		ec_setupheader(&(port->txbuf[i]));
		port->rxbufstat[i] = EC_BUF_EMPTY;
	}
	ec_setupheader(&(port->txbuf2));

	return 1;
}

/** Close sockets used
 * @param[in] port        = port context struct
 * @return 0
 */
int ecx_closenic(ecx_portt *port)
{
	if (port->sockhandle >= 0)
	{
		//Ethernet_Close();
		port->sockhandle = -1;
	}

	if ((port->redport) && (port->redport->sockhandle >= 0))
	{
		port->redport->sockhandle = -1;
	}

	return 0;
}

/** Fill buffer with ethernet header structure.
 * Destination MAC is always broadcast.
 * Ethertype is always ETH_P_ECAT.
 * @param[out] p = buffer
 */
void ec_setupheader(void *p)
{
	ec_etherheadert *bp;
	bp = p;
	bp->da0 = htons(0xffff);
	bp->da1 = htons(0xffff);
	bp->da2 = htons(0xffff);
	bp->sa0 = htons(priMAC[0]);
	bp->sa1 = htons(priMAC[1]);
	bp->sa2 = htons(priMAC[2]);
	bp->etype = htons(ETH_P_ECAT);
}
