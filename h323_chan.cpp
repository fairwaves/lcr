///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// PBX4Linux                                                                 //
//                                                                           //
//---------------------------------------------------------------------------//
// Copyright: Andreas Eversberg                                              //
//                                                                           //
// H323_chan class                                                           //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include "main.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

//
// constructor
//
H323_chan::H323_chan(const PString &token, BOOL isEncoding) : PIndirectChannel()
{
	d_token = token;
//	d_isEncoding = isEncoding;

	PDEBUG(DEBUG_H323, "H323 channel  constuctor of channel (%scoding)\n", (isEncoding)?"en":"de");

	transfering = FALSE;
}


//
// destructor
//
H323_chan::~H323_chan(void)
{

	PDEBUG(DEBUG_H323, "H323 channel  destuctor\n");
}


//
// Closes the
//
BOOL H323_chan::Close(void)
{
	PDEBUG(DEBUG_H323, "H323 channel  Close\n");

	return TRUE;
}


//
// IsOpen
//
BOOL H323_chan::IsOpen(void) const
{
	PDEBUG(DEBUG_H323, "H323 channel  IsOpen\n");

	return TRUE;
}


//
// Read
//
BOOL H323_chan::Read(void *buf, PINDEX len)
{
	int nr_words;
	class H323Port *port;
	const unsigned char *token_string = d_token;
	PTime Now;
	PTimeInterval diff;

	nr_words = len/2;

//	cout << "H323 channel  Read " << nr_words << " words" << endl;
	mutex_h323.Wait();

	if (!(port = (class H323Port *)find_port_with_token((char *)token_string)))
	{
		PERROR("H323 channel  Read() cannot find port with token %s\n", token_string);
		mutex_h323.Signal();
		lastReadCount = 0;
		return FALSE;
	}

	nr_words = port->read_audio((unsigned char *)buf, nr_words, 0);

	mutex_h323.Signal();

	// delay
	if (!transfering)
	{
		PDEBUG(DEBUG_H323, "H323 channel  Read(%s) sending to h323 the first time\n", token_string);
		start = Now;
		transfering = TRUE;
		elapsed = 0;
	}
	diff = Now-start;
	elapsed += nr_words*125;
	if (elapsed > (diff.GetMilliSeconds()*1000))
		usleep(elapsed - (diff.GetMilliSeconds()*1000));

	lastReadCount = 2 * nr_words;

	return TRUE;
}


//
// Write
//
BOOL H323_chan::Write(const void *buf, PINDEX len)
{
	int nr_words;
	class H323Port *port;
	const unsigned char *token_string = d_token;
	PTime Now;
	PTimeInterval diff;
	unsigned char *data_temp;
	unsigned long length_temp;
	struct message *message;

	nr_words = len / 2;

//	cout << "H323 channel  Write " << nr_words << " words" << endl;
	mutex_h323.Wait();

	if (!(port = (class H323Port *)find_port_with_token((char *)token_string)))
	{
		PERROR("H323 channel  Write() cannot find port with token %s\n", token_string);
		mutex_h323.Signal();
		lastReadCount = 0;
		return FALSE;
	}

	// send data message
	length_temp = len;
	data_temp = (unsigned char *)buf;
	while(length_temp)
	{
		message = message_create(port->p_serial, ACTIVE_EPOINT(port->p_epointlist), PORT_TO_EPOINT, MESSAGE_DATA);
		message->param.data.len = (length_temp>sizeof(message->param.data.data))?sizeof(message->param.data.data):length_temp;
		memcpy(message->param.data.data, data_temp, message->param.data.len);
		message->param.data.compressed = 0;
/*		{ // testin with law data
			int i=0;
			while (i<message->param.data.len)
			{
				((unsigned char *)message->param.data.data)[i] = audio_s16_to_law[((signed short*)data_temp)[i] & 0xffff];
				i++;
			}
		}
		message->param.data.len = message->param.data.len/2;
		message->param.data.compressed = 1;
*/
		message->param.data.port_type = port->p_type; 
		message->param.data.port_id = port->p_serial;
		message_put(message);
		if (length_temp <= sizeof(message->param.data.data))
			break;
		data_temp += sizeof(message->param.data.data);
		length_temp -= sizeof(message->param.data.data);
	}

	mutex_h323.Signal();

	// delay
	if (!transfering)
	{
		PDEBUG(DEBUG_H323, "H323 channel  Write(%s) receiving from h323 the first time\n", token_string);
		start = Now;
		transfering = TRUE;
		elapsed = 0;
	}
	diff = Now-start;
	elapsed += nr_words*125;
	if (elapsed > (diff.GetMilliSeconds()*1000))
		usleep(elapsed - (diff.GetMilliSeconds()*1000));

	lastWriteCount = 2 * nr_words;

	return TRUE;
}

