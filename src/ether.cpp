/*
 *  ether.cpp - Ethernet device driver
 *
 *  Basilisk II (C) 1997-2001 Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 *  SEE ALSO
 *    Inside Macintosh: Devices, chapter 1 "Device Manager"
 *    Inside Macintosh: Networking, chapter 11 "Ethernet, Token Ring, and FDDI"
 *    Inside AppleTalk, chapter 3 "Ethernet and TokenTalk Link Access Protocols"
 */

#include "sysdeps.h"

#include <string.h>
#include <map>

#if SUPPORTS_UDP_TUNNEL
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netdb.h>
#include <errno.h>
#endif

#include "cpu_emulation.h"
#include "main.h"
#include "macos_util.h"
#include "emul_op.h"
#include "prefs.h"
#include "ether.h"
#include "ether_defs.h"

#ifndef NO_STD_NAMESPACE
using std::map;
#endif

#define DEBUG 0
#include "debug.h"

#define MONITOR 0


// Global variables
uint8 ether_addr[6];			// Ethernet address (set by ether_init())
static bool net_open = false;	// Flag: initialization succeeded, network device open (set by EtherInit())

static bool udp_tunnel = false;	// Flag: tunnelling AppleTalk over UDP using BSD socket API
static uint16 udp_port;
static int udp_socket = -1;

// Mac address of driver data in MacOS RAM
uint32 ether_data = 0;

// Attached network protocols for UDP tunneling, maps protocol type to MacOS handler address
static map<uint16, uint32> udp_protocols;


/*
 *  Initialization
 */

void EtherInit(void)
{
	net_open = false;
	udp_tunnel = false;

#if SUPPORTS_UDP_TUNNEL
	// UDP tunnelling requested?
	if (PrefsFindBool("udptunnel")) {
		udp_tunnel = true;
		udp_port = PrefsFindInt32("udpport");

		// Open UDP socket
		udp_socket = socket(PF_INET, SOCK_DGRAM, 0);
		if (udp_socket < 0) {
			perror("socket");
			return;
		}

		// Bind to specified address and port
		struct sockaddr_in sa;
		memset(&sa, 0, sizeof(sa));
		sa.sin_family = AF_INET;
		sa.sin_addr.s_addr = INADDR_ANY;
		sa.sin_port = htons(udp_port);
		if (bind(udp_socket, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
			perror("bind");
			close(udp_socket);
			udp_socket = -1;
			return;
		}

		// Retrieve local IP address (or at least one of them)
		socklen_t sa_len = sizeof(sa);
		getsockname(udp_socket, (struct sockaddr *)&sa, &sa_len);
		uint32 udp_ip = sa.sin_addr.s_addr;
		if (udp_ip == INADDR_ANY || udp_ip == INADDR_LOOPBACK) {
			char name[256];
			gethostname(name, sizeof(name));
			struct hostent *local = gethostbyname(name);
			if (local)
				udp_ip = *(uint32 *)local->h_addr_list[0];
		}
		udp_ip = ntohl(udp_ip);

		// Construct dummy Ethernet address from local IP address
		ether_addr[0] = 'B';
		ether_addr[1] = '2';
		ether_addr[2] = udp_ip >> 24;
		ether_addr[3] = udp_ip >> 16;
		ether_addr[4] = udp_ip >> 8;
		ether_addr[5] = udp_ip;
		D(bug("Ethernet address %02x %02x %02x %02x %02x %02x\n", ether_addr[0], ether_addr[1], ether_addr[2], ether_addr[3], ether_addr[4], ether_addr[5]));

		// Set socket options
		int on = 1;
		setsockopt(udp_socket, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
		ioctl(udp_socket, FIONBIO, &on);

		// Start thread for packet reception
		if (!ether_start_udp_thread(udp_socket)) {
			close(udp_socket);
			udp_socket = -1;
			return;
		}

		net_open = true;
	} else
#endif
		if (ether_init())
			net_open = true;
}


/*
 *  Deinitialization
 */

void EtherExit(void)
{
	if (net_open) {
#if SUPPORTS_UDP_TUNNEL
		if (udp_tunnel) {
			if (udp_socket >= 0) {
				ether_stop_udp_thread();
				close(udp_socket);
				udp_socket = -1;
			}
		} else
#endif
			ether_exit();
		net_open = false;
	}
}


/*
 *  Reset
 */

void EtherReset(void)
{
	udp_protocols.clear();
	ether_reset();
}


/*
 *  Check whether Ethernet address is AppleTalk broadcast address
 */

static inline bool is_apple_talk_broadcast(uint8 *p)
{
	return p[0] == 0x09 && p[1] == 0x00 && p[2] == 0x07
	    && p[3] == 0xff && p[4] == 0xff && p[5] == 0xff;
}


/*
 *  Driver Open() routine
 */

int16 EtherOpen(uint32 pb, uint32 dce)
{
	D(bug("EtherOpen\n"));

	// Allocate driver data
	M68kRegisters r;
	r.d[0] = SIZEOF_etherdata;
	Execute68kTrap(0xa71e, &r);		// NewPtrSysClear()
	if (r.a[0] == 0)
		return openErr;
	ether_data = r.a[0];
	D(bug(" data %08x\n", ether_data));

	WriteMacInt16(ether_data + ed_DeferredTask + qType, dtQType);
	WriteMacInt32(ether_data + ed_DeferredTask + dtAddr, ether_data + ed_Code);
	WriteMacInt32(ether_data + ed_DeferredTask + dtParam, ether_data + ed_Result);
															// Deferred function for signalling that packet write is complete (pointer to mydtResult in a1)
	WriteMacInt16(ether_data + ed_Code, 0x2019);			//  move.l	(a1)+,d0	(result)
	WriteMacInt16(ether_data + ed_Code + 2, 0x2251);		//  move.l	(a1),a1		(dce)
	WriteMacInt32(ether_data + ed_Code + 4, 0x207808fc);	//  move.l	JIODone,a0
	WriteMacInt16(ether_data + ed_Code + 8, 0x4ed0);		//  jmp		(a0)

	WriteMacInt32(ether_data + ed_DCE, dce);
															// ReadPacket/ReadRest routines
	WriteMacInt16(ether_data + ed_ReadPacket, 0x6010);		//	bra		2
	WriteMacInt16(ether_data + ed_ReadPacket + 2, 0x3003);	//  move.w	d3,d0
	WriteMacInt16(ether_data + ed_ReadPacket + 4, 0x9041);	//  sub.w	d1,d0
	WriteMacInt16(ether_data + ed_ReadPacket + 6, 0x4a43);	//  tst.w	d3
	WriteMacInt16(ether_data + ed_ReadPacket + 8, 0x6702);	//  beq		1
	WriteMacInt16(ether_data + ed_ReadPacket + 10, M68K_EMUL_OP_ETHER_READ_PACKET);
	WriteMacInt16(ether_data + ed_ReadPacket + 12, 0x3600);	//1 move.w	d0,d3
	WriteMacInt16(ether_data + ed_ReadPacket + 14, 0x7000);	//  moveq	#0,d0
	WriteMacInt16(ether_data + ed_ReadPacket + 16, 0x4e75);	//  rts
	WriteMacInt16(ether_data + ed_ReadPacket + 18, M68K_EMUL_OP_ETHER_READ_PACKET);	//2
	WriteMacInt16(ether_data + ed_ReadPacket + 20, 0x4a43);	//  tst.w	d3
	WriteMacInt16(ether_data + ed_ReadPacket + 22, 0x4e75);	//  rts
	return 0;
}


/*
 *  Driver Control() routine
 */

int16 EtherControl(uint32 pb, uint32 dce)
{
	uint16 code = ReadMacInt16(pb + csCode);
	D(bug("EtherControl %d\n", code));
	switch (code) {
		case 1:					// KillIO
			return -1;

		case kENetAddMulti:		// Add multicast address
			D(bug(" AddMulti %08x%04x\n", ReadMacInt32(pb + eMultiAddr), ReadMacInt16(pb + eMultiAddr + 4)));
			if (net_open && !udp_tunnel)
				return ether_add_multicast(pb);
			return noErr;

		case kENetDelMulti:		// Delete multicast address
			D(bug(" DelMulti %08x%04x\n", ReadMacInt32(pb + eMultiAddr), ReadMacInt16(pb + eMultiAddr + 4)));
			if (net_open && !udp_tunnel)
				return ether_del_multicast(pb);
			return noErr;

		case kENetAttachPH: {	// Attach protocol handler
			uint16 type = ReadMacInt16(pb + eProtType);
			uint32 handler = ReadMacInt32(pb + ePointer);
			D(bug(" AttachPH prot %04x, handler %08x\n", type, handler));
			if (net_open) {
				if (udp_tunnel) {
					if (udp_protocols.find(type) != udp_protocols.end())
						return lapProtErr;
					udp_protocols[type] = handler;
				} else
					return ether_attach_ph(type, handler);
			}
			return noErr;
		}

		case kENetDetachPH: {	// Detach protocol handler
			uint16 type = ReadMacInt16(pb + eProtType);
			D(bug(" DetachPH prot %04x\n", type));
			if (net_open) {
				if (udp_tunnel) {
					if (udp_protocols.erase(type) == 0)
						return lapProtErr;
				} else
					return ether_detach_ph(type);
			}
			return noErr;
		}

		case kENetWrite: {		// Transmit raw Ethernet packet
			uint32 wds = ReadMacInt32(pb + ePointer);
			D(bug(" EtherWrite\n"));
			if (ReadMacInt16(wds) < 14)
				return eLenErr;	// Header incomplete
			if (net_open) {
#if SUPPORTS_UDP_TUNNEL
				if (udp_tunnel) {

					// Copy packet to buffer
					uint8 packet[1514];
					int len = ether_wds_to_buffer(wds, packet);

					// Set source address and extract destination address
					memcpy(packet + 6, ether_addr, 6);
					uint32 dest_ip;
					if (packet[0] == 'B' && packet[1] == '2')
						dest_ip = (packet[2] << 24) | (packet[3] << 16) | (packet[4] << 8) | packet[5];
					else if (is_apple_talk_broadcast(packet))
						dest_ip = INADDR_BROADCAST;
					else
						return eMultiErr;

#if MONITOR
					bug("Sending Ethernet packet:\n");
					for (int i=0; i<len; i++) {
						bug("%02x ", packet[i]);
					}
					bug("\n");
#endif

					// Send packet
					struct sockaddr_in sa;
					sa.sin_family = AF_INET;
					sa.sin_addr.s_addr = htonl(dest_ip);
					sa.sin_port = htons(udp_port);
					if (sendto(udp_socket, packet, len, 0, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
						D(bug("WARNING: Couldn't transmit packet\n"));
						return excessCollsns;
					}
				} else
#endif
					return ether_write(wds);
			}
			return noErr;
		}

		case kENetGetInfo: {	// Get device information/statistics
			D(bug(" GetInfo buf %08x, size %d\n", ReadMacInt32(pb + ePointer), ReadMacInt16(pb + eBuffSize)));

			// Collect info (only ethernet address)
			uint8 buf[18];
			memset(buf, 0, 18);
			memcpy(buf, ether_addr, 6);

			// Transfer info to supplied buffer
			int16 size = ReadMacInt16(pb + eBuffSize);
			if (size > 18)
				size = 18;
			WriteMacInt16(pb + eDataSize, size);	// Number of bytes actually written
			Host2Mac_memcpy(ReadMacInt32(pb + ePointer), buf, size);
			return noErr;
		}

		case kENetSetGeneral:	// Set general mode (always in general mode)
			D(bug(" SetGeneral\n"));
			return noErr;

		default:
			printf("WARNING: Unknown EtherControl(%d)\n", code);
			return controlErr;
	}
}


/*
 *  Ethernet ReadPacket routine
 */

void EtherReadPacket(uint8 **src, uint32 &dest, uint32 &len, uint32 &remaining)
{
	D(bug("EtherReadPacket src %p, dest %08x, len %08x, remaining %08x\n", *src, dest, len, remaining));
	uint32 todo = len > remaining ? remaining : len;
	Host2Mac_memcpy(dest, *src, todo);
	*src += todo;
	dest += todo;
	len -= todo;
	remaining -= todo;
}


#if SUPPORTS_UDP_TUNNEL
/*
 *  Read packet from UDP socket
 */

void ether_udp_read(uint8 *packet, int length, struct sockaddr_in *from)
{
	// Drop packets sent by us
	if (memcmp(packet + 6, ether_addr, 6) == 0)
		return;

#if MONITOR
	bug("Receiving Ethernet packet:\n");
	for (int i=0; i<length; i++) {
		bug("%02x ", packet[i]);
	}
	bug("\n");
#endif

	// Get packet type
	uint16 type = (packet[12] << 8) | packet[13];

	// Look for protocol
	uint16 search_type = (type <= 1500 ? 0 : type);
	if (udp_protocols.find(search_type) == udp_protocols.end())
		return;
	uint32 handler = udp_protocols[search_type];
	if (handler == 0)
		return;

	// Copy header to RHA
	Host2Mac_memcpy(ether_data + ed_RHA, packet, 14);
	D(bug(" header %08x%04x %08x%04x %04x\n", ReadMacInt32(ether_data + ed_RHA), ReadMacInt16(ether_data + ed_RHA + 4), ReadMacInt32(ether_data + ed_RHA + 6), ReadMacInt16(ether_data + ed_RHA + 10), ReadMacInt16(ether_data + ed_RHA + 12)));

	// Call protocol handler
	M68kRegisters r;
	r.d[0] = type;									// Packet type
	r.d[1] = length - 14;							// Remaining packet length (without header, for ReadPacket)
	r.a[0] = (uint32)packet + 14;					// Pointer to packet (host address, for ReadPacket)
	r.a[3] = ether_data + ed_RHA + 14;				// Pointer behind header in RHA
	r.a[4] = ether_data + ed_ReadPacket;			// Pointer to ReadPacket/ReadRest routines
	D(bug(" calling protocol handler %08x, type %08x, length %08x, data %08x, rha %08x, read_packet %08x\n", handler, r.d[0], r.d[1], r.a[0], r.a[3], r.a[4]));
	Execute68k(handler, &r);
}
#endif
