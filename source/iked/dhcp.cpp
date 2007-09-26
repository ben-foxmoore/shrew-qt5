
/*
 * Copyright (c) 2007
 *      Shrew Soft Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Redistributions in any form must be accompanied by information on
 *    how to obtain complete source code for the software and any
 *    accompanying software that uses the software.  The source code
 *    must either be included in the distribution or be available for no
 *    more than the cost of distribution plus a nominal fee, and must be
 *    freely redistributable under reasonable conditions.  For an
 *    executable file, complete source code means the source code for all
 *    modules it contains.  It does not include source code for modules or
 *    files that typically accompany the major components of the operating
 *    system on which the executable file runs.
 *
 * THIS SOFTWARE IS PROVIDED BY SHREW SOFT INC ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, OR
 * NON-INFRINGEMENT, ARE DISCLAIMED.  IN NO EVENT SHALL SHREW SOFT INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * AUTHOR : Matthew Grooms
 *          mgrooms@shrew.net
 *
 */

#include "iked.h"

long _IKED::socket_dhcp_setup( IDB_TUNNEL * tunnel )
{
	//
	// close dhcp socket
	//

	socket_dhcp_close( tunnel );

	//
	// create dhcp transaction id
	//

	rand_bytes( &tunnel->dhcp_xid, 4 );

	//
	// create dhcp socket
	//

	tunnel->dhcp_sock = socket( PF_INET, SOCK_DGRAM, IPPROTO_UDP );
	if( tunnel->dhcp_sock < 0 )
	{
		log.txt( LLOG_ERROR, "!! : failed to create DHCP socket\n" );
		return LIBIKE_SOCKET;
	}

	//
	// bind socket to address and port
	//

	struct sockaddr_in saddr = tunnel->saddr_l.saddr4;
	saddr.sin_port = htons( UDP_PORT_DHCPC );

	if( bind( tunnel->dhcp_sock, ( sockaddr * ) &saddr, sizeof( saddr ) ) < 0 )
	{
		log.txt( LLOG_ERROR, "!! : failed to bind DHCP socket\n" );
		return LIBIKE_SOCKET;
	}

	//
	// set non-blocking operation
	//

#ifdef WIN32

	u_long arg = 1;
	if( ioctlsocket( tunnel->dhcp_sock, FIONBIO, &arg ) < 0 )
	{
		log.txt( LLOG_ERROR, "!! : failed to set DHCP socket to non-blocking\n" );
		return LIBIKE_SOCKET;
	}

#else

	if( fcntl( tunnel->dhcp_sock, F_SETFL, O_NONBLOCK ) < 0 )
	{
		log.txt( LLOG_ERROR, "!! : failed to set DHCP socket to non-blocking\n" );
		return LIBIKE_SOCKET;
	}

#endif

	char txtaddr[ LIBIKE_MAX_TEXTADDR ];
	text_addr( txtaddr, saddr.sin_addr );

	log.txt( LLOG_DEBUG, "ii : setup DHCP socket for address %s\n", txtaddr );

	return LIBIKE_OK;
}

long _IKED::socket_dhcp_close( IDB_TUNNEL * tunnel )
{
	//
	// close dhcp socket
	//

	if( tunnel->dhcp_sock != -1 )
	{

#ifdef WIN32

		closesocket( tunnel->dhcp_sock );

#else

		close( tunnel->dhcp_sock );

#endif

		tunnel->dhcp_sock = -1;
	}

	return LIBIKE_OK;
}

long _IKED::socket_dhcp_create( IDB_TUNNEL * tunnel )
{
	//
	// setup dhcp socket
	//

	socket_dhcp_setup( tunnel );

	//
	// create dhcp ipsec policies
	//

	policy_dhcp_create( tunnel );

	return LIBIKE_OK;
}

long _IKED::socket_dhcp_remove( IDB_TUNNEL * tunnel )
{
	//
	// remove dhcp ipsec policies
	//

	policy_dhcp_remove( tunnel );

	//
	// close dhcp socket
	//

	socket_dhcp_close( tunnel );

	return LIBIKE_OK;
}

long _IKED::socket_dhcp_send( IDB_TUNNEL * tunnel, PACKET & packet )
{
	//
	// send the packet
	//

	struct sockaddr_in saddr = tunnel->saddr_r.saddr4;
	saddr.sin_port = htons( UDP_PORT_DHCPS );

	int rslt;
	rslt = sendto(
		tunnel->dhcp_sock,
		packet.text(),
		packet.size(),
		0,
		( sockaddr * ) &saddr,
		sizeof( saddr ) );

	if( rslt < 0 )
	{
		log.txt( LLOG_ERROR, "!! : failed to write to BPF filter\n" );
		return LIBIKE_SOCKET;
	}

	return LIBIKE_OK;
}

long _IKED::socket_dhcp_recv( IDB_TUNNEL * tunnel, PACKET & packet )
{
	char buff[ 1024 ];
	long size = 1024;

	size = recv(
		tunnel->dhcp_sock,
		buff,
		size,
		0 );

	if( size < 0 )
		return LIBIKE_NODATA;

	packet.add(
		buff,
		size );

	return LIBIKE_OK;
}

long _IKED::process_dhcp_send( IDB_PH1 * ph1 )
{
	//
	// DHCP over IPsec discover packet
	//

	if( !( ph1->tunnel->state & TSTATE_RECV_CONFIG ) )
	{
		//
		// create dhcp discover packet
		//

		in_addr src, dst;
		memcpy( &src, &ph1->tunnel->saddr_l.saddr4.sin_addr, sizeof( src ) );
		memcpy( &dst, &ph1->tunnel->saddr_r.saddr4.sin_addr, sizeof( dst ) );

		PACKET packet;

		DHCP_HEADER dhcp_head;
		memset( &dhcp_head, 0, sizeof( dhcp_head ) );

		dhcp_head.magic = DHCP_MAGIC;
		dhcp_head.op = BOOTP_REQUEST;			// bootp request
		dhcp_head.htype = BOOTP_HW_IPSEC;		// bootp hardware type
		dhcp_head.hlen = 6;						// hardware address length
		dhcp_head.xid = ph1->tunnel->dhcp_xid;	// transaction id

		dhcp_head.chaddr[ 0 ] = 0x40;			// locally administered unicast MAC
		memcpy(									// local ipv4 interface address
			dhcp_head.chaddr + 2,
			&src, sizeof( src ) );

		packet.add(
			&dhcp_head,
			sizeof( dhcp_head ) );

		packet.add_byte( DHCP_OPT_MSGTYPE );	// message type
		packet.add_byte( 1 );					// opt size
		packet.add_byte( DHCP_MSG_DISCOVER );	// message type value

		packet.add_byte( DHCP_OPT_CLIENTID );	// message type
		packet.add_byte( 7 );					// opt size
		packet.add_byte( BOOTP_HW_IPSEC );		// client hw type
		packet.add( dhcp_head.chaddr, 6 );		// client id value

		//
		// send the packet
		//

		log.txt( LLOG_DEBUG, "ii : sending DHCP over IPsec discover\n" );

		socket_dhcp_send( ph1->tunnel, packet );

		ph1->event_dhcp.retry++;
	}

	//
	// DHCP over IPsec request packet
	//

	if( ph1->tunnel->state & TSTATE_RECV_CONFIG )
	{
		//
		// create dhcp request packet
		//

		in_addr src, dst;
		memcpy( &src, &ph1->tunnel->saddr_l.saddr4.sin_addr, sizeof( src ) );
		memcpy( &dst, &ph1->tunnel->saddr_r.saddr4.sin_addr, sizeof( dst ) );

		PACKET packet;

		DHCP_HEADER dhcp_head;
		memset( &dhcp_head, 0, sizeof( dhcp_head ) );

		dhcp_head.magic = DHCP_MAGIC;
		dhcp_head.op = BOOTP_REQUEST;			// bootp request
		dhcp_head.htype = BOOTP_HW_IPSEC;		// bootp hardware type
		dhcp_head.hlen = 6;						// hardware address length
		dhcp_head.xid = ph1->tunnel->dhcp_xid;	// transaction id

		dhcp_head.chaddr[ 0 ] = 0x40;			// locally administered unicast MAC
		memcpy(									// local ip interface address
			dhcp_head.chaddr + 2,
			&src, sizeof( src ) );

		packet.add(
			&dhcp_head,
			sizeof( dhcp_head ) );

		packet.add_byte( DHCP_OPT_MSGTYPE );	// message type
		packet.add_byte( 1 );					// opt size
		packet.add_byte( DHCP_MSG_REQUEST );	// message type value

		packet.add_byte( DHCP_OPT_SERVER );			// server id
		packet.add_byte( 4 );						// opt size
		packet.add( &ph1->tunnel->xconf.dhcp, 4 );	// server id value

		packet.add_byte( DHCP_OPT_ADDRESS );		// requested address
		packet.add_byte( 4 );						// opt size
		packet.add( &ph1->tunnel->xconf.addr, 4 );	// address value

		packet.add_byte( DHCP_OPT_CLIENTID );	// client id
		packet.add_byte( 7 );					// opt size
		packet.add_byte( BOOTP_HW_IPSEC );		// client hw type
		packet.add( dhcp_head.chaddr, 6 );		// client id value

		//
		// send the packet
		//

		log.txt( LLOG_DEBUG, "ii : sending DHCP over IPsec request\n" );

		socket_dhcp_send( ph1->tunnel, packet );

		ph1->event_dhcp.retry++;
	}

	return LIBIKE_OK;
}

long _IKED::process_dhcp_recv( IDB_PH1 * ph1 )
{
	PACKET packet;
	long result = socket_dhcp_recv( ph1->tunnel, packet );
	if( result != LIBIKE_OK )
		return LIBIKE_FAILED;

	//
	// examine the dhcp reply header
	//

	DHCP_HEADER dhcp_head;
	if( !packet.get( &dhcp_head, sizeof( dhcp_head ) ) )
	{
		log.txt( LLOG_ERROR, "!! : malformed DHCP reply packet\n" );
		ph1->tunnel->dec( true );
		return LIBIKE_FAILED;
	}

	if( ( dhcp_head.op != BOOTP_REPLY ) ||			// bootp reply
		( dhcp_head.htype != BOOTP_HW_IPSEC ) ||	// bootp hardware type
		( dhcp_head.hlen != 6 ) ||					// hardware address length
		( dhcp_head.magic != DHCP_MAGIC ) )			// magic cookie
	{
		log.txt( LLOG_ERROR, "!! : invalid DHCP reply parameters\n" );
		ph1->tunnel->dec( true );
		return LIBIKE_FAILED;
	}

	//
	// examine the dhcp reply options
	//

	log.txt( LLOG_DEBUG, "ii : reading DHCP reply options\n" );

	IKE_XCONF	config;
	uint8_t		type;
	char		txtaddr[ LIBIKE_MAX_TEXTADDR ];
	bool		end = false;

	while( !end )
	{
		unsigned char opt;
		unsigned char len;

		if( !packet.get_byte( opt ) )
			break;

		if( !packet.get_byte( len ) )
			break;

		if( len > ( packet.size() - packet.oset() ) )
			break;

		switch( opt )
		{
			case DHCP_OPT_MSGTYPE:
			{
				config.addr.s_addr = dhcp_head.yiaddr;
				text_addr( txtaddr, config.addr );

				packet.get_byte( type );

				switch( type )
				{
					case DHCP_MSG_OFFER:
						log.txt( LLOG_DEBUG, "ii : - message type = offer ( %s )\n", txtaddr );
						break;

					case DHCP_MSG_ACK:
						log.txt( LLOG_DEBUG, "ii : - message type = ack ( %s )\n", txtaddr );
						break;

					default:
						log.txt( LLOG_ERROR, "!! : invalid DHCP message type ( %i )\n", int( type ) );
						ph1->tunnel->dec( true );
						return LIBIKE_FAILED;
				}

				break;
			}

			case DHCP_OPT_SUBMASK:
				if( len >= 4 )
				{
					packet.get( &config.mask, 4 );
					len -= 4;
					text_addr( txtaddr, config.mask );
					log.txt( LLOG_DEBUG, "ii : - IP4 Netmask = %s\n", txtaddr );
				}
				packet.get_null( len );
				break;

			case DHCP_OPT_SERVER:
				if( len >= 4 )
				{
					packet.get( &config.dhcp, 4 );
					len -= 4;
					text_addr( txtaddr, config.dhcp );
					log.txt( LLOG_DEBUG, "ii : - IP4 DHCP Server = %s\n", txtaddr );
				}
				packet.get_null( len );
				break;

			case DHCP_OPT_LEASE:
				if( len >= 4 )
				{
					uint32_t d, h, m, s;
					packet.get_quad( s, true );
					ph1->event_dhcp.lease = s;
					len -= 4;

					d = s / 86400;
					s = s % 86400;
					h = s / 3600;
					s = s % 3600;
					m = s / 60;
					s = s % 60;

					log.txt( LLOG_DEBUG,
						"ii : - IP4 DHCP lease = %d days, %d hours, %d mins, %d secs\n",
						d, h, m, s );
				}
				packet.get_null( len );
				break;

			case DHCP_OPT_DNSS:
				if( len >= 4 )
				{
					packet.get( &config.dnss, 4 );
					len -= 4;
					text_addr( txtaddr, config.dnss );
					log.txt( LLOG_DEBUG, "ii : - IP4 DNS Server = %s\n", txtaddr );
				}
				packet.get_null( len );
				break;

			case DHCP_OPT_NBNS:
				if( len >= 4 )
				{
					packet.get( &config.nbns, false );
					len -= 4;
					text_addr( txtaddr, config.nbns );
					log.txt( LLOG_DEBUG, "ii : - IP4 WINS Server = %s\n", txtaddr );
				}
				packet.get_null( len );
				break;

			case DHCP_OPT_DOMAIN:
			{
				long tmp = len;
				if( tmp > 255 )
					tmp = 255;
				if( len >= 1 )
				{
					packet.get( config.suffix, tmp );
					config.suffix[ tmp ] = 0;
					log.txt( LLOG_DEBUG, "ii : - DNS Suffix = %s\n", config.suffix );
				}
				packet.get_null( len - tmp );
				break;
			}
			case DHCP_OPT_CLIENTID:
				log.txt( LLOG_DEBUG, "ii : - clientid ( %i bytes )\n", len );
				packet.get_null( len );
				break;

			case DHCP_OPT_END:
				end = true;
				break;

			default:
				log.txt( LLOG_DECODE, "ii : - unknown option ( %02x )\n", opt );
				packet.get_null( len );
				break;
		}
	}

	//
	// DHCP offer
	//

	if( type == DHCP_MSG_OFFER )
	{
		if( !( ph1->tunnel->state & TSTATE_RECV_CONFIG ) )
		{
			//
			// accept supported options
			//

			ph1->tunnel->xconf.dhcp = config.dhcp;

			if( ph1->tunnel->xconf.opts & IPSEC_OPTS_ADDR )
				ph1->tunnel->xconf.addr = config.addr;

			if( ph1->tunnel->xconf.opts & IPSEC_OPTS_MASK )
				ph1->tunnel->xconf.mask = config.mask;

			if( ph1->tunnel->xconf.opts & IPSEC_OPTS_DNSS )
				ph1->tunnel->xconf.dnss = config.dnss;

			if( ph1->tunnel->xconf.opts & IPSEC_OPTS_DOMAIN )
				memcpy( ph1->tunnel->xconf.suffix, config.suffix, CONF_STRLEN );

			if( ph1->tunnel->xconf.opts & IPSEC_OPTS_NBNS )
				ph1->tunnel->xconf.nbns = config.nbns;

			ph1->event_dhcp.retry = 0;

			ph1->tunnel->state |= TSTATE_RECV_CONFIG;
		}
	}

	//
	// DHCP acknowledge
	//

	if( type == DHCP_MSG_ACK )
	{
		if( ph1->tunnel->state & TSTATE_RECV_CONFIG )
		{
			//
			// setup lease times
			//

			ph1->event_dhcp.retry = 0;
			ph1->event_dhcp.renew = time( NULL );
			ph1->event_dhcp.renew += ph1->event_dhcp.lease / 2;

			ph1->tunnel->state |= TSTATE_RECV_ACK;
		}
	}

	return LIBIKE_OK;
}
