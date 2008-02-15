
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

//
// generic exchange handle event functions
//

bool _ITH_EVENT_RESEND::func()
{
	if( attempt >= iked.retry_count )
	{
		iked.log.txt( LLOG_INFO,
				"ii : resend limit exceeded for %s exchange\n",
				xch->name() );

		xch->status( XCH_STATUS_DEAD, XCH_FAILED_TIMEOUT, 0 );
		xch->dec( true );

		return false;
	}

	xch->lock.lock();

	long count = ipqueue.count();
	long index = 0;

	for( ; index < count; index++ )
	{
		PACKET_IP packet;
		ipqueue.get( packet, index );

		iked.send_ip(
			packet );
	}

	xch->lock.unlock();

	iked.log.txt( LLOG_INFO,
		"ii : resend %i packet(s) for %s exchange\n",
		count,
		xch->name() );

	attempt++;

	return true;
}

//
// generic exchange handle class
//

_IDB_XCH::_IDB_XCH()
{
	tunnel = NULL;

	xch_status = XCH_STATUS_LARVAL;
	xch_errorcode = XCH_NORMAL;
	xch_notifycode = 0;

	initiator = false;
	exchange = 0;

	lstate = 0;
	xstate = 0;

	dh = NULL;
	dh_size = 0;

	hash_size = 0;

	lock.setname( "xch" );

	//
	// initialize event info
	//

	event_resend.xch = this;
}

_IDB_XCH::~_IDB_XCH()
{
	resend_clear();
}

XCH_STATUS _IDB_XCH::status()
{
	lock.lock();

	XCH_STATUS cur_status = xch_status;

	lock.unlock();

	return xch_status;
}

XCH_STATUS _IDB_XCH::status( XCH_STATUS status, XCH_ERRORCODE errorcode, uint16_t notifycode )
{
	lock.lock();

	XCH_STATUS cur_status = xch_status;

	if( cur_status != XCH_STATUS_DEAD )
	{
		cur_status = xch_status = status;
		xch_errorcode = errorcode;
		xch_notifycode = notifycode;

		if( status == XCH_STATUS_DEAD )
			setflags( IDB_FLAG_DEAD );
	}

	lock.unlock();

	return cur_status;
}

bool _IDB_XCH::resend_queue( PACKET_IP & packet )
{
	//
	// queue our new packet
	//

	lock.lock();

	bool added = event_resend.ipqueue.add( packet );

	lock.unlock();

	return added;
}

bool _IDB_XCH::resend_sched()
{
	//
	// reset our attempt counter
	//

	event_resend.attempt = 0;

	//
	// add our resend event
	//

	inc( true );
	event_resend.delay = iked.retry_delay * 1000;

	iked.ith_timer.add( &event_resend );
	
	return true;
}

void _IDB_XCH::resend_clear()
{
	//
	// remove resend event and clear our queue
	//

	if( iked.ith_timer.del( &event_resend ) )
		dec( false );

	event_resend.ipqueue.flush();
}

