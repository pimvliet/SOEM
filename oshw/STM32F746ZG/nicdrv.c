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
#include "oshw.h"
#include "socket_w5500.h"
#include "w5500.h"

int16_t sendRAW(uint8_t sn, uint8_t* buf, uint16_t len)
{
	while(1)
	{
		uint16_t freesize = getSn_TX_FSR(sn);
		if (getSn_SR(sn) == SOCK_CLOSED)
			return -1;
		if (len <= freesize)
			break;
	}

	wiz_send_data(sn, buf, len);
	setSn_CR(sn, Sn_CR_SEND);

	while(1)
	{
		uint8_t tmp = getSn_IR(sn);
		if (tmp & Sn_IR_SENDOK)
		{
			setSn_IR(sn, Sn_IR_SENDOK);
			break;
		}
		else if (tmp & Sn_IR_TIMEOUT)
		{
			setSn_IR(sn, Sn_IR_TIMEOUT);
			return -1;
		}
	}
	return len;
}

uint16_t recvRAW(uint8_t sn, uint8_t* buf, uint16_t bufsize)
{
	uint16_t len = getSn_RX_RSR(sn);

	if (len > 0)
	{
		uint8_t head[2];
		uint16_t data_len = 0;

		wiz_recv_data(sn, head, 2);
		setSn_CR(sn, Sn_CR_RECV);

		data_len = head[0];
		data_len = (data_len << 8) + head[1];
		data_len -= 2;

		if (data_len > bufsize)
		{
			wiz_recv_ignore(sn, data_len);
			setSn_CR(sn, Sn_CR_RECV);
			return 0;
		}

		wiz_recv_data(sn, buf, data_len);
		setSn_CR(sn, Sn_CR_RECV);

		if ((buf[0] & 0x01) || memcpy(&buf[0], priMAC, 6) == 0)
			return data_len;
		else
			return 0;
	}
	return 0;
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
const uint16 priMAC[3] =
{ 0x0101, 0x0101, 0x0101 };
/** Secondary source MAC address used for EtherCAT. */
const uint16 secMAC[3] =
{ 0x0404, 0x0404, 0x0404 };

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

#if 0
	wizchip_sw_reset();

	setSn_RXBUF_SIZE(0, 16);
	setSn_TXBUF_SIZE(0, 16);

	uint8_t priMAC2[6] = {0x01, 0x01, 0x01, 0x01, 0x01, 0x01};
	setSHAR(priMAC2);

	int retval;
	setSn_MR(0, Sn_MR_MACRAW);
	setSn_CR(0, Sn_CR_OPEN);
	retval = getSn_MR(0);
	while(getSn_CR(0));
	while(getSn_SR(0) == SOCK_CLOSED);
	retval = getSn_SR(0);
	if (retval != SOCK_MACRAW)
	{
		// Failed to put socket 0 into MACRaw mode
		return 0;
	}

	*psock = 0;
#else
	int rval = socket(0, Sn_MR_MACRAW, 30303, 0);
	*psock = rval;
#endif

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
		close(port->sockhandle);
	}

	if ((port->redport) && (port->redport->sockhandle >= 0))
	{
		close(port->redport->sockhandle);
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
	bp->da0 = oshw_htons(0xffff);
	bp->da1 = oshw_htons(0xffff);
	bp->da2 = oshw_htons(0xffff);
	bp->sa0 = oshw_htons(priMAC[0]);
	bp->sa1 = oshw_htons(priMAC[1]);
	bp->sa2 = oshw_htons(priMAC[2]);
	bp->etype = oshw_htons(ETH_P_ECAT);
}

/** Get new frame identifier index and allocate corresponding rx buffer.
 * @param[in] port        = port context struct
 * @return new index.
 */
uint8 ecx_getindex(ecx_portt *port)
{
	uint8 idx;
	uint8 cnt;

	idx = port->lastidx + 1;
	/* index can't be larger than buffer array */
	if (idx >= EC_MAXBUF)
	{
		idx = 0;
	}
	cnt = 0;
	/* try to find unused index */
	while ((port->rxbufstat[idx] != EC_BUF_EMPTY) && (cnt < EC_MAXBUF))
	{
		idx++;
		cnt++;
		if (idx >= EC_MAXBUF)
		{
			idx = 0;
		}
	}
	port->rxbufstat[idx] = EC_BUF_ALLOC;
	if (port->redstate != ECT_RED_NONE)
		port->redport->rxbufstat[idx] = EC_BUF_ALLOC;
	port->lastidx = idx;

	return idx;
}

/** Set rx buffer status.
 * @param[in] port        = port context struct
 * @param[in] idx      = index in buffer array
 * @param[in] bufstat  = status to set
 */
void ecx_setbufstat(ecx_portt *port, uint8 idx, int bufstat)
{
	port->rxbufstat[idx] = bufstat;
	if (port->redstate != ECT_RED_NONE)
		port->redport->rxbufstat[idx] = bufstat;
}

/** Transmit buffer over socket (non blocking).
 * @param[in] port        = port context struct
 * @param[in] idx      = index in tx buffer array
 * @param[in] stacknumber   = 0=Primary 1=Secondary stack
 * @return socket send result
 */
int ecx_outframe(ecx_portt *port, uint8 idx, int stacknumber)
{
	int lp, rval;
	ec_stackT *stack;

	if (!stacknumber)
		stack = &(port->stack);
	else
		stack = &(port->redport->stack);

	lp = (*stack->txbuflength)[idx];
	(*stack->rxbufstat)[idx] = EC_BUF_TX;

	rval = sendRAW(*stack->sock, (*stack->txbuf)[idx], lp);

	if (rval < 0)
	{
		(*stack->rxbufstat)[idx] = EC_BUF_EMPTY;
	}
	return rval;
}

/** Transmit buffer over socket (non blocking).
 * @param[in] port        = port context struct
 * @param[in] idx = index in tx buffer array
 * @return socket send result
 */
int ecx_outframe_red(ecx_portt *port, uint8 idx)
{
	ec_etherheadert *ehp;
	int rval;

	ehp = (ec_etherheadert*) &(port->txbuf[idx]);

	ehp->sa1 = oshw_htons(priMAC[1]);

	rval = ecx_outframe(port, idx, 0);
	/* Redundancy is not supported using a single port */
#if 0
	ec_comt *datagramP;

	if (port->redstate != ECT_RED_NONE)
	{
		ehp = (ec_etherheadert*) &(port->txbuf2);

		datagramP = (ec_comt*) &(port->txbuf2[ETH_HEADERSIZE]);
		datagramP->index = idx;

		ehp->sa1 = oshw_htons(secMAC[1]);

		port->redport->rxbufstat[idx] = EC_BUF_TX;
		if (send(port->redport->sockhandle, (uint8_t*) &(port->txbuf2),
				port->txbuflength2) < 0)
		{
			port->redport->rxbufstat[idx] = EC_BUF_EMPTY;
		}
	}
#endif

	return rval;
}

/** Non blocking read of socket. Put frame in temporary buffer.
 * @param[in] port        = port context struct
 * @param[in] stacknumber = 0=primary 1=secondary stack
 * @return >0 if frame is available and read
 */
static int ecx_recvpkt(ecx_portt *port, int stacknumber)
{
	int lp, bytesrx;
	ec_stackT *stack;

	if (!stacknumber)
		stack = &(port->stack);
	else
		stack = &(port->redport->stack);

	lp = sizeof(port->tempinbuf);
#if 1
	bytesrx = recvRAW(*stack->sock, (*stack->tempbuf), lp);
#else
	uint8_t dummyip[4];
	uint16_t dummyport;
	bytesrx = recvfrom(*stack->sock, (*stack->tempbuf), lp, dummyip, &dummyport);
#endif
	port->tempinbufs = bytesrx;

	return (bytesrx > 0);
}

/** Non blocking receive frame function. Uses RX buffer and index to combine
 * read frame with transmitted frame. To compensate for received frames that
 * are out-of-order all frames are stored in their respective indexed buffer.
 * If a frame was placed in the buffer previously, the function retrieves it
 * from that buffer index without calling ec_recvpkt. If the requested index
 * is not already in the buffer it calls ec_recvpkt to fetch it. There are
 * three options now, 1 no frame read, so exit. 2 frame read but other
 * than requested index, store in buffer and exit. 3 frame read with matching
 * index, store in buffer, set completed flag in buffer status and exit.
 *
 * @param[in] port        = port context struct
 * @param[in] idx         = requested index of frame
 * @param[in] stacknumber = 0=primary 1=secondary stack
 * @return Workcounter if a frame is found with corresponding index, otherwise
 * EC_NOFRAME or EC_OTHERFRAME.
 */
int ecx_inframe(ecx_portt *port, uint8 idx, int stacknumber)
{
	uint16 l;
	int rval;
	uint8 idxf;
	ec_etherheadert *ehp;
	ec_comt *ecp;
	ec_stackT *stack;
	ec_bufT *rxbuf;

	if (!stacknumber)
		stack = &(port->stack);
	else
		stack = &(port->redport->stack);

	rval = EC_NOFRAME;
	rxbuf = &(*stack->rxbuf)[idx];

	if ((idx < EC_MAXBUF) && ((*stack->rxbufstat)[idx] == EC_BUF_RCVD))
	{
		l = (*rxbuf)[0] + ((uint16) ((*rxbuf)[1] & 0x0f) << 8);
		rval = ((*rxbuf)[l] + ((uint16) (*rxbuf)[l + 1] << 8));

		(*stack->rxbufstat)[idx] = EC_BUF_COMPLETE;
	}
	else
	{
		if (ecx_recvpkt(port, stacknumber))
		{
			rval = EC_OTHERFRAME;
			ehp = (ec_etherheadert*) (stack->tempbuf);

			if (ehp->etype == oshw_htons(ETH_P_ECAT))
			{
				ecp = (ec_comt*) (&(*stack->tempbuf)[ETH_HEADERSIZE]);
				l = etohs(ecp->elength) & 0x0fff;
				idxf = ecp->index;

				if (idxf == idx)
				{
					memcpy(rxbuf, &(*stack->tempbuf)[ETH_HEADERSIZE],
							(*stack->txbuflength)[idx] - ETH_HEADERSIZE);
					rval = ((*rxbuf)[l] + ((uint16) ((*rxbuf)[l + 1]) << 8));
					(*stack->rxbufstat)[idx] = EC_BUF_COMPLETE;
					(*stack->rxsa)[idx] = oshw_ntohs(ehp->sa1);
				}
				else
				{
					if (idxf < EC_MAXBUF
							&& (*stack->rxbufstat)[idxf] == EC_BUF_TX)
					{
						rxbuf = &(*stack->rxbuf)[idxf];
						memcpy(rxbuf, &(*stack->tempbuf)[ETH_HEADERSIZE],
								(*stack->txbuflength)[idxf] - ETH_HEADERSIZE);
						(*stack->rxbufstat)[idxf] = EC_BUF_RCVD;
						(*stack->rxsa)[idxf] = oshw_ntohs(ehp->sa1);
					}
				}
			}
		}
	}

	return rval;
}

/** Blocking redundant receive frame function. If redundant mode is not active then
 * it skips the secondary stack and redundancy functions. In redundant mode it waits
 * for both (primary and secondary) frames to come in. The result goes in an decision
 * tree that decides, depending on the route of the packet and its possible missing arrival,
 * how to reroute the original packet to get the data in an other try.
 *
 * @param[in] port        = port context struct
 * @param[in] idx = requested index of frame
 * @param[in] timer = absolute timeout time
 * @return Workcounter if a frame is found with corresponding index, otherwise
 * EC_NOFRAME.
 */
static int ecx_waitinframe_red(ecx_portt *port, uint8 idx, osal_timert *timer)
{
	osal_timert timer2;
	int wkc = EC_NOFRAME;
	int wkc2 = EC_NOFRAME;
	int primrx, secrx;

	if (port->redstate == ECT_RED_NONE)
		wkc2 = 0;
	do
	{
		if (wkc <= EC_NOFRAME)
			wkc = ecx_inframe(port, idx, 0);
		if (port->redstate != ECT_RED_NONE)
		{
			if (wkc2 <= EC_NOFRAME)
				wkc2 = ecx_inframe(port, idx, 1);
		}
	} while (((wkc <= EC_NOFRAME) || (wkc2 <= EC_NOFRAME))
			&& !osal_timer_is_expired(timer));

	if (port->redstate != ECT_RED_NONE)
	{
		primrx = 0;
		if (wkc > EC_NOFRAME)
			primrx = port->rxsa[idx];

		secrx = 0;
		if (wkc2 > EC_NOFRAME)
			secrx = port->redport->rxsa[idx];

		if ((primrx == RX_SEC) && (secrx == RX_PRIM))
		{
			memcpy(&(port->rxbuf[idx]), &(port->redport->rxbuf[idx]), port->txbuflength[idx] - ETH_HEADERSIZE);
			wkc = wkc2;
		}

		if (((primrx == 0) && (secrx == RX_SEC)) || ((primrx == RX_PRIM) && (secrx == RX_SEC)))
		{
			if ((primrx == RX_PRIM) && (secrx == RX_SEC))
			memcpy(&(port->txbuf[idx][ETH_HEADERSIZE]), &(port->rxbuf[idx]), port->txbuflength[idx] - ETH_HEADERSIZE);
			osal_timer_start(&timer2, EC_TIMEOUTRET);
			ecx_outframe(port, idx, 1);
			do
			{
				wkc2 = ecx_inframe(port, idx, 1);
			}while ((wkc2 <= EC_NOFRAME) && !osal_timer_is_expired(&timer2));
			if (wkc2 > EC_NOFRAME)
			{
				memcpy(&(port->rxbuf[idx]), &(port->redport->rxbuf[idx]), port->txbuflength[idx] - ETH_HEADERSIZE);
				wkc = wkc2;
			}
		}
	}
	return wkc;
}

/** Blocking receive frame function. Calls ec_waitinframe_red().
 * @param[in] port        = port context struct
 * @param[in] idx       = requested index of frame
 * @param[in] timeout   = timeout in us
 * @return Workcounter if a frame is found with corresponding index, otherwise
 * EC_NOFRAME.
 */
int ecx_waitinframe(ecx_portt *port, uint8 idx, int timeout)
{
	int wkc;
	osal_timert timer;

	osal_timer_start(&timer, timeout);
	wkc = ecx_waitinframe_red(port, idx, &timer);

	return wkc;
}

/** Blocking send and receive frame function. Used for non processdata frames.
 * A datagram is build into a frame and transmitted via this function. It waits
 * for an answer and returns the workcounter. The function retries if time is
 * left and the result is WKC=0 or no frame received.
 *
 * The function calls ec_outframe_red() and ec_waitinframe_red().
 *
 * @param[in] port        = port context struct
 * @param[in] idx      = index of frame
 * @param[in] timeout  = timeout in us
 * @return Workcounter or EC_NOFRAME
 */
int ecx_srconfirm(ecx_portt *port, uint8 idx, int timeout)
{
	int wkc = EC_NOFRAME;
	osal_timert timer1, timer2;

	osal_timer_start(&timer1, timeout);
	do
	{
		/* tx frame on primary and if in redundant mode a dummy on secondary */
		ecx_outframe_red(port, idx);
		if (timeout < EC_TIMEOUTRET)
		{
			osal_timer_start(&timer2, timeout);
		}
		else
		{
			/* normally use partial timeout for rx */
			osal_timer_start(&timer2, EC_TIMEOUTRET);
		}
		/* get frame from primary or if in redundant mode possibly from secondary */
		wkc = ecx_waitinframe_red(port, idx, &timer2);
		/* wait for answer with WKC>=0 or otherwise retry until timeout */
	} while ((wkc <= EC_NOFRAME) && !osal_timer_is_expired(&timer1));

	return wkc;
}

#ifdef EC_VER1
int ec_setupnic(const char *ifname, int secondary)
{
	return ecx_setupnic(&ecx_port, ifname, secondary);
}

int ec_closenic(void)
{
	return ecx_closenic(&ecx_port);
}

uint8 ec_getindex(void)
{
	return ecx_getindex(&ecx_port);
}

void ec_setbufstat(uint8 idx, int bufstat)
{
	ecx_setbufstat(&ecx_port, idx, bufstat);
}

int ec_outframe(uint8 idx, int stacknumber)
{
	return ecx_outframe(&ecx_port, idx, stacknumber);
}

int ec_outframe_red(uint8 idx)
{
	return ecx_outframe_red(&ecx_port, idx);
}

int ec_inframe(uint8 idx, int stacknumber)
{
	return ecx_inframe(&ecx_port, idx, stacknumber);
}

int ec_waitinframe(uint8 idx, int timeout)
{
	return ecx_waitinframe(&ecx_port, idx, timeout);
}

int ec_srconfirm(uint8 idx, int timeout)
{
	return ecx_srconfirm(&ecx_port, idx, timeout);
}
#endif
