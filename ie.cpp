
/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** information elements encode and decode                                    **
**                                                                           **
\*****************************************************************************/ 

/*
 the pointer of enc_ie_* always points to the IE itself
 if qi is not NULL (TE-mode), offset is set
*/

/* support stuff */
static void strnncpy(unsigned char *dest, unsigned char *src, int len, int dst_len)
{
	if (len > dst_len-1)
		len = dst_len-1;
	UNCPY((char *)dest, (char *)src, len);
	dest[len] = '\0';
}


/* IE_COMPLETE */
void Pdss1::enc_ie_complete(unsigned char **ntmode, msg_t *msg, int complete)
{
	unsigned char *p;
	Q931_info_t *qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);

	if (complete<0 || complete>1)
	{
		PERROR("complete(%d) is out of range.\n", complete);
		return;
	}

	if (complete)
	{
		add_trace("complete", NULL, NULL);
		p = msg_put(msg, 1);
		if (p_m_d_ntmode)
		{
			*ntmode = p;
		} else
			qi->sending_complete.off = p - (unsigned char *)qi - sizeof(Q931_info_t);
		p[0] = IE_COMPLETE;
	}
}

void Pdss1::dec_ie_complete(unsigned char *p, Q931_info_t *qi, int *complete)
{
	*complete = 0;
	if (!p_m_d_ntmode)
	{
		if (qi->sending_complete.off)
			*complete = 1;
	} else
	if (p)
		*complete = 1;

	if (*complete)
		add_trace("complete", NULL, NULL);
}


/* IE_BEARER */
void Pdss1::enc_ie_bearer(unsigned char **ntmode, msg_t *msg, int coding, int capability, int mode, int rate, int multi, int user)
{
	unsigned char *p;
	Q931_info_t *qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);
	int l;

	if (coding<0 || coding>3)
	{
		PERROR("coding(%d) is out of range.\n", coding);
		return;
	}
	if (capability<0 || capability>31)
	{
		PERROR("capability(%d) is out of range.\n", capability);
		return;
	}
	if (mode<0 || mode>3)
	{
		PERROR("mode(%d) is out of range.\n", mode);
		return;
	}
	if (rate<0 || rate>31)
	{
		PERROR("rate(%d) is out of range.\n", rate);
		return;
	}
	if (multi>127)
	{
		PERROR("multi(%d) is out of range.\n", multi);
		return;
	}
	if (user>31)
	{
		PERROR("user L1(%d) is out of range.\n", user);
		return;
	}
	if (rate!=24 && multi>=0)
	{
		PERROR("multi(%d) is only possible if rate(%d) would be 24.\n", multi, rate);
		multi = -1;
	}

	add_trace("bearer", "coding", "%d", coding);
	add_trace("bearer", "capability", "%d", capability);
	add_trace("bearer", "mode", "%d", mode);
	add_trace("bearer", "rate", "%d", rate);
	add_trace("bearer", "multi", "%d", multi);
	add_trace("bearer", "user", "%d", user);

	l = 2 + (multi>=0) + (user>=0);
	p = msg_put(msg, l+2);
	if (p_m_d_ntmode)
		*ntmode = p+1;
	else
		qi->bearer_capability.off = p - (unsigned char *)qi - sizeof(Q931_info_t);
	p[0] = IE_BEARER;
	p[1] = l;
	p[2] = 0x80 + (coding<<5) + capability;
	p[3] = 0x80 + (mode<<5) + rate;
	if (multi >= 0)
		p[4] = 0x80 + multi;
	if (user >= 0)
		p[4+(multi>=0)] = 0xa0 + user;
}

void Pdss1::dec_ie_bearer(unsigned char *p, Q931_info_t *qi, int *coding, int *capability, int *mode, int *rate, int *multi, int *user)
{
	*coding = -1;
	*capability = -1;
	*mode = -1;
	*rate = -1;
	*multi = -1;
	*user = -1;

	if (!p_m_d_ntmode)
	{
		p = NULL;
		if (qi->bearer_capability.off)
			p = (unsigned char *)qi + sizeof(Q931_info_t) + qi->bearer_capability.off + 1;
	}
	if (!p)
		return;
	if (p[0] < 2)
	{
		PERROR("IE too short (%d).\n", p[0]);
		return;
	}

	*coding = (p[1]&0x60) >> 5;
	*capability = p[1] & 0x1f;
	if (p[0]>=2)
	{
		*mode = (p[2]&0x60) >> 5;
		*rate = p[2] & 0x1f;
	}
	if (p[0]>=3 && *rate==0x18)
	{
		*multi = p[3] & 0x7f;
		if (p[0]>=4)
			*user = p[4] & 0x1f;
	} else
	{
		if (p[0]>=3)
			*user = p[3] & 0x1f;
	}

	add_trace("bearer", "coding", "%d", *coding);
	add_trace("bearer", "capability", "%d", *capability);
	add_trace("bearer", "mode", "%d", *mode);
	add_trace("bearer", "rate", "%d", *rate);
	add_trace("bearer", "multi", "%d", *multi);
	add_trace("bearer", "user", "%d", *user);
}


/* IE_HLC */
void Pdss1::enc_ie_hlc(unsigned char **ntmode, msg_t *msg, int coding, int interpretation, int presentation, int hlc, int exthlc)
{
	unsigned char *p;
	Q931_info_t *qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);
	int l;

	if (coding<0 || coding>3)
	{
		PERROR("coding(%d) is out of range.\n", coding);
		return;
	}
	if (interpretation<0 || interpretation>7)
	{
		PERROR("interpretation(%d) is out of range.\n", interpretation);
		return;
	}
	if (presentation<0 || presentation>3)
	{
		PERROR("presentation(%d) is out of range.\n", presentation);
		return;
	}
	if (hlc<0 || hlc>127)
	{
		PERROR("hlc(%d) is out of range.\n", hlc);
		return;
	}
	if (exthlc>127)
	{
		PERROR("hlc(%d) is out of range.\n", exthlc);
		return;
	}

	add_trace("hlc", "coding", "%d", coding);
	add_trace("hlc", "interpretation", "%d", interpretation);
	add_trace("hlc", "presentation", "%d", presentation);
	add_trace("hlc", "hlc", "%d", hlc);
	if (exthlc >= 0)
		add_trace("hlc", "exthlc", "%d", exthlc);

	l = 2 + (exthlc>=0);
	p = msg_put(msg, l+2);
	if (p_m_d_ntmode)
		*ntmode = p+1;
	else
		qi->hlc.off = p - (unsigned char *)qi - sizeof(Q931_info_t);
	p[0] = IE_HLC;
	p[1] = l;
	p[2] = 0x80 + (coding<<5) + (interpretation<<2) + presentation;
	if (exthlc >= 0)
	{
		p[3] = hlc;
		p[4] = 0x80 + exthlc;
	} else
		p[3] = 0x80 + hlc;
}

void Pdss1::dec_ie_hlc(unsigned char *p, Q931_info_t *qi, int *coding, int *interpretation, int *presentation, int *hlc, int *exthlc)
{
	*coding = -1;
	*interpretation = -1;
	*presentation = -1;
	*hlc = -1;
	*exthlc = -1;

	if (!p_m_d_ntmode)
	{
		p = NULL;
		if (qi->hlc.off)
			p = (unsigned char *)qi + sizeof(Q931_info_t) + qi->hlc.off + 1;
	}
	if (!p)
		return;
	if (p[0] < 2)
	{
		PERROR("IE too short (%d).\n", p[0]);
		return;
	}

	*coding = (p[1]&0x60) >> 5;
	*interpretation = (p[1]&0x1c) >> 2;
	*presentation = p[1] & 0x03;
	*hlc = p[2] & 0x7f;
	if (p[0]>=3)
	{
		*exthlc = p[3] & 0x7f;
	}

	add_trace("hlc", "coding", "%d", *coding);
	add_trace("hlc", "interpretation", "%d", *interpretation);
	add_trace("hlc", "presentation", "%d", *presentation);
	add_trace("hlc", "hlc", "%d", *hlc);
	if (*exthlc >= 0)
		add_trace("hlc", "exthlc", "%d", *exthlc);
}


/* IE_CALL_ID */
void Pdss1::enc_ie_call_id(unsigned char **ntmode, msg_t *msg, unsigned char *callid, int callid_len)
{
	unsigned char *p;
	Q931_info_t *qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);
	int l;

	char buffer[25];
	int i;

	if (!callid || callid_len<=0)
	{
		return;
	}
	if (callid_len > 8)
	{
		PERROR("callid_len(%d) is out of range.\n", callid_len);
		return;
	}

	i = 0;
	while(i < callid_len)
	{
		UPRINT(buffer+(i*3), " %02x", callid[i]);
		i++;
	}
		
	add_trace("callid", NULL, "%s", buffer[0]?buffer+1:"<none>");

	l = callid_len;
	p = msg_put(msg, l+2);
	if (p_m_d_ntmode)
		*ntmode = p+1;
	else
		qi->call_id.off = p - (unsigned char *)qi - sizeof(Q931_info_t);
	p[0] = IE_CALL_ID;
	p[1] = l;
	memcpy(p+2, callid, callid_len);
}

void Pdss1::dec_ie_call_id(unsigned char *p, Q931_info_t *qi, unsigned char *callid, int *callid_len)
{
	char buffer[25];
	int i;

	*callid_len = -1;

	if (!p_m_d_ntmode)
	{
		p = NULL;
		if (qi->call_id.off)
			p = (unsigned char *)qi + sizeof(Q931_info_t) + qi->call_id.off + 1;
	}
	if (!p)
		return;
	if (p[0] > 8)
	{
		PERROR("IE too long (%d).\n", p[0]);
		return;
	}

	*callid_len = p[0];
	memcpy(callid, p+1, *callid_len);

	i = 0;
	while(i < *callid_len)
	{
		UPRINT(buffer+(i*3), " %02x", callid[i]);
		i++;
	}
		
	add_trace("callid", NULL, "%s", buffer[0]?buffer+1:"<none>");
}


/* IE_CALLED_PN */
void Pdss1::enc_ie_called_pn(unsigned char **ntmode, msg_t *msg, int type, int plan, unsigned char *number)
{
	unsigned char *p;
	Q931_info_t *qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);
	int l;

	if (type<0 || type>7)
	{
		PERROR("type(%d) is out of range.\n", type);
		return;
	}
	if (plan<0 || plan>15)
	{
		PERROR("plan(%d) is out of range.\n", plan);
		return;
	}
	if (!number[0])
	{
		PERROR("number is not given.\n");
		return;
	}

	add_trace("called_pn", "type", "%d", type);
	add_trace("called_pn", "plan", "%d", plan);
	add_trace("called_pn", "number", "%s", number);

	l = 1+strlen((char *)number);
	p = msg_put(msg, l+2);
	if (p_m_d_ntmode)
		*ntmode = p+1;
	else
		qi->called_nr.off = p - (unsigned char *)qi - sizeof(Q931_info_t);
	p[0] = IE_CALLED_PN;
	p[1] = l;
	p[2] = 0x80 + (type<<4) + plan;
	UNCPY((char *)p+3, (char *)number, strlen((char *)number));
}

void Pdss1::dec_ie_called_pn(unsigned char *p, Q931_info_t *qi, int *type, int *plan, unsigned char *number, int number_len)
{
	*type = -1;
	*plan = -1;
	*number = '\0';

	if (!p_m_d_ntmode)
	{
		p = NULL;
		if (qi->called_nr.off)
			p = (unsigned char *)qi + sizeof(Q931_info_t) + qi->called_nr.off + 1;
	}
	if (!p)
		return;
	if (p[0] < 2)
	{
		PERROR("IE too short (%d).\n", p[0]);
		return;
	}

	*type = (p[1]&0x70) >> 4;
	*plan = p[1] & 0xf;
	strnncpy(number, p+2, p[0]-1, number_len);

	add_trace("called_pn", "type", "%d", *type);
	add_trace("called_pn", "plan", "%d", *plan);
	add_trace("called_pn", "number", "%s", number);
}


/* IE_CALLING_PN */
void Pdss1::enc_ie_calling_pn(unsigned char **ntmode, msg_t *msg, int type, int plan, int present, int screen, unsigned char *number)
{
	unsigned char *p;
	Q931_info_t *qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);
	int l;

	if (type<0 || type>7)
	{
		PERROR("type(%d) is out of range.\n", type);
		return;
	}
	if (plan<0 || plan>15)
	{
		PERROR("plan(%d) is out of range.\n", plan);
		return;
	}
	if (present>3)
	{
		PERROR("present(%d) is out of range.\n", present);
		return;
	}
	if (present >= 0) if (screen<0 || screen>3)
	{
		PERROR("screen(%d) is out of range.\n", screen);
		return;
	}

	add_trace("calling_pn", "type", "%d", type);
	add_trace("calling_pn", "plan", "%d", plan);
	add_trace("calling_pn", "present", "%d", present);
	add_trace("calling_pn", "screen", "%d", screen);
	add_trace("calling_pn", "number", "%s", number);

	l = 1;
	if (number) if (number[0])
		l += strlen((char *)number);
	if (present >= 0)
		l += 1;
	p = msg_put(msg, l+2);
	if (p_m_d_ntmode)
		*ntmode = p+1;
	else
		qi->calling_nr.off = p - (unsigned char *)qi - sizeof(Q931_info_t);
	p[0] = IE_CALLING_PN;
	p[1] = l;
	if (present >= 0)
	{
		p[2] = 0x00 + (type<<4) + plan;
		p[3] = 0x80 + (present<<5) + screen;
		if (number) if (number[0])
			UNCPY((char *)p+4, (char *)number, strlen((char *)number));
	} else
	{
		p[2] = 0x80 + (type<<4) + plan;
		if (number) if (number[0])
			UNCPY((char *)p+3, (char *)number, strlen((char *)number));
	}
}

void Pdss1::dec_ie_calling_pn(unsigned char *p, Q931_info_t *qi, int *type, int *plan, int *present, int *screen, unsigned char *number, int number_len)
{
	*type = -1;
	*plan = -1;
	*present = -1;
	*screen = -1;
	*number = '\0';

	if (!p_m_d_ntmode)
	{
		p = NULL;
		if (qi->calling_nr.off)
			p = (unsigned char *)qi + sizeof(Q931_info_t) + qi->calling_nr.off + 1;
	}
	if (!p)
		return;
	if (p[0] < 1)
	{
		PERROR("IE too short (%d).\n", p[0]);
		return;
	}

	*type = (p[1]&0x70) >> 4;
	*plan = p[1] & 0xf;
	if (!(p[1] & 0x80))
	{
		if (p[0] < 2)
		{
			PERROR("IE too short (%d).\n", p[0]);
			return;
		}
		*present = (p[2]&0x60) >> 5;
		*screen = p[2] & 0x3;
		strnncpy(number, p+3, p[0]-2, number_len);
	} else
	{
		strnncpy(number, p+2, p[0]-1, number_len);
	}

	add_trace("calling_pn", "type", "%d", *type);
	add_trace("calling_pn", "plan", "%d", *plan);
	add_trace("calling_pn", "present", "%d", *present);
	add_trace("calling_pn", "screen", "%d", *screen);
	add_trace("calling_pn", "number", "%s", *number);
}


/* IE_CONNECTED_PN */
void Pdss1::enc_ie_connected_pn(unsigned char **ntmode, msg_t *msg, int type, int plan, int present, int screen, unsigned char *number)
{
	unsigned char *p;
	Q931_info_t *qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);
	int l;

	if (type<0 || type>7)
	{
		PERROR("type(%d) is out of range.\n", type);
		return;
	}
	if (plan<0 || plan>15)
	{
		PERROR("plan(%d) is out of range.\n", plan);
		return;
	}
	if (present>3)
	{
		PERROR("present(%d) is out of range.\n", present);
		return;
	}
	if (present >= 0) if (screen<0 || screen>3)
	{
		PERROR("screen(%d) is out of range.\n", screen);
		return;
	}

	add_trace("connect_pn", "type", "%d", type);
	add_trace("connect_pn", "plan", "%d", plan);
	add_trace("connect_pn", "present", "%d", present);
	add_trace("connect_pn", "screen", "%d", screen);
	add_trace("connect_pn", "number", "%s", number);

	l = 1;
	if (number) if (number[0])
		l += strlen((char *)number);
	if (present >= 0)
		l += 1;
	p = msg_put(msg, l+2);
	if (p_m_d_ntmode)
		*ntmode = p+1;
	else
		qi->connected_nr.off = p - (unsigned char *)qi - sizeof(Q931_info_t);
	p[0] = IE_CONNECT_PN;
	p[1] = l;
	if (present >= 0)
	{
		p[2] = 0x00 + (type<<4) + plan;
		p[3] = 0x80 + (present<<5) + screen;
		if (number) if (number[0])
			UNCPY((char *)p+4, (char *)number, strlen((char *)number));
	} else
	{
		p[2] = 0x80 + (type<<4) + plan;
		if (number) if (number[0])
			UNCPY((char *)p+3, (char *)number, strlen((char *)number));
	}
}

void Pdss1::dec_ie_connected_pn(unsigned char *p, Q931_info_t *qi, int *type, int *plan, int *present, int *screen, unsigned char *number, int number_len)
{
	*type = -1;
	*plan = -1;
	*present = -1;
	*screen = -1;
	*number = '\0';

	if (!p_m_d_ntmode)
	{
		p = NULL;
		if (qi->connected_nr.off)
			p = (unsigned char *)qi + sizeof(Q931_info_t) + qi->connected_nr.off + 1;
	}
	if (!p)
		return;
	if (p[0] < 1)
	{
		PERROR("IE too short (%d).\n", p[0]);
		return;
	}

	*type = (p[1]&0x70) >> 4;
	*plan = p[1] & 0xf;
	if (!(p[1] & 0x80))
	{
		if (p[0] < 2)
		{
			PERROR("IE too short (%d).\n", p[0]);
			return;
		}
		*present = (p[2]&0x60) >> 5;
		*screen = p[2] & 0x3;
		strnncpy(number, p+3, p[0]-2, number_len);
	} else
	{
		strnncpy(number, p+2, p[0]-1, number_len);
	}

	add_trace("connect_pn", "type", "%d", *type);
	add_trace("connect_pn", "plan", "%d", *plan);
	add_trace("connect_pn", "present", "%d", *present);
	add_trace("connect_pn", "screen", "%d", *screen);
	add_trace("connect_pn", "number", "%s", number);
}


/* IE_CAUSE */
void Pdss1::enc_ie_cause(unsigned char **ntmode, msg_t *msg, int location, int cause)
{
	unsigned char *p;
	Q931_info_t *qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);
	int l;

	if (location<0 || location>7)
	{
		PERROR("location(%d) is out of range.\n", location);
		return;
	}
	if (cause<0 || cause>127)
	{
		PERROR("cause(%d) is out of range.\n", cause);
		return;
	}

	add_trace("cause", "location", "%d", location);
	add_trace("cause", "value", "%d", cause);

	l = 2;
	p = msg_put(msg, l+2);
	if (p_m_d_ntmode)
		*ntmode = p+1;
	else
		qi->cause.off = p - (unsigned char *)qi - sizeof(Q931_info_t);
	p[0] = IE_CAUSE;
	p[1] = l;
	p[2] = 0x80 + location;
	p[3] = 0x80 + cause;
}
void enc_ie_cause_standalone(unsigned char **ntmode, msg_t *msg, int location, int cause)
{
	unsigned char *p = msg_put(msg, 4);
	Q931_info_t *qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);
	if (ntmode)
		*ntmode = p+1;
	else
		qi->cause.off = p - (unsigned char *)qi - sizeof(Q931_info_t);
	p[0] = IE_CAUSE;
	p[1] = 2;
	p[2] = 0x80 + location;
	p[3] = 0x80 + cause;
}


void Pdss1::dec_ie_cause(unsigned char *p, Q931_info_t *qi, int *location, int *cause)
{
	*location = -1;
	*cause = -1;

	if (!p_m_d_ntmode)
	{
		p = NULL;
		if (qi->cause.off)
			p = (unsigned char *)qi + sizeof(Q931_info_t) + qi->cause.off + 1;
	}
	if (!p)
		return;
	if (p[0] < 2)
	{
		PERROR("IE too short (%d).\n", p[0]);
		return;
	}

	*location = p[1] & 0x0f;
	*cause = p[2] & 0x7f;

	add_trace("cause", "location", "%d", *location);
	add_trace("cause", "value", "%d", *cause);
}


/* IE_CHANNEL_ID */
void Pdss1::enc_ie_channel_id(unsigned char **ntmode, msg_t *msg, int exclusive, int channel)
{
	unsigned char *p;
	Q931_info_t *qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);
	int l;
	int pri = p_m_mISDNport->pri;

	if (exclusive<0 || exclusive>1)
	{
		PERROR("exclusive(%d) is out of range.\n", exclusive);
		return;
	}
	if ((channel<=0 && channel!=CHANNEL_NO && channel!=CHANNEL_ANY)
	 || (!pri && channel>2)
	 || (pri && channel>127)
	 || (pri && channel==16))
	{
		PERROR("channel(%d) is out of range.\n", channel);
		return;
	}

	add_trace("channel_id", "exclusive", "%d", exclusive);
	add_trace("channel_id", "channel", "%d", channel);

	if (!pri)
	{
		/* BRI */
		l = 1;
		p = msg_put(msg, l+2);
		if (p_m_d_ntmode)
			*ntmode = p+1;
		else
			qi->channel_id.off = p - (unsigned char *)qi - sizeof(Q931_info_t);
		p[0] = IE_CHANNEL_ID;
		p[1] = l;
		if (channel == CHANNEL_NO)
			channel = 0;
		else if (channel == CHANNEL_ANY)
			channel = 3;
		p[2] = 0x80 + (exclusive<<3) + channel;
	} else
	{
		/* PRI */
		if (channel == CHANNEL_NO) /* no channel */
			return; /* IE not present */
		if (channel == CHANNEL_ANY) /* any channel */
		{
			l = 1;
			p = msg_put(msg, l+2);
			if (p_m_d_ntmode)
				*ntmode = p+1;
			else
				qi->channel_id.off = p - (unsigned char *)qi - sizeof(Q931_info_t);
			p[0] = IE_CHANNEL_ID;
			p[1] = l;
			p[2] = 0x80 + 0x20 + 0x03;
			return; /* end */
		}
		l = 3;
		p = msg_put(msg, l+2);
		if (p_m_d_ntmode)
			*ntmode = p+1;
		else
			qi->channel_id.off = p - (unsigned char *)qi - sizeof(Q931_info_t);
		p[0] = IE_CHANNEL_ID;
		p[1] = l;
		p[2] = 0x80 + 0x20 + (exclusive<<3) + 0x01;
		p[3] = 0x80 + 3; /* CCITT, Number, B-type */
		p[4] = 0x80 + channel;
	}
}

void Pdss1::dec_ie_channel_id(unsigned char *p, Q931_info_t *qi, int *exclusive, int *channel)
{
	int pri = p_m_mISDNport->pri;

	*exclusive = -1;
	*channel = -1;

	if (!p_m_d_ntmode)
	{
		p = NULL;
		if (qi->channel_id.off)
			p = (unsigned char *)qi + sizeof(Q931_info_t) + qi->channel_id.off + 1;
	}
	if (!p)
		return;
	if (p[0] < 1)
	{
		PERROR("IE too short (%d).\n", p[0]);
		return;
	}

	if (p[1] & 0x40)
	{
		PERROR("refering to channels of other interfaces is not supported.\n");
		return;
	}
	if (p[1] & 0x04)
	{
		PERROR("using d-channel is not supported.\n");
		return;
	}

	*exclusive = (p[1]&0x08) >> 3;
	if (!pri)
	{
		/* BRI */
		if (p[1] & 0x20)
		{
			PERROR("extended channel ID with non PRI interface.\n");
			return;
		}
		*channel = p[1] & 0x03;
		if (*channel == 3)
			*channel = CHANNEL_ANY;
		else if (*channel == 0)
			*channel = CHANNEL_NO;
	} else
	{
		/* PRI */
		if (p[0] < 1)
		{
			PERROR("IE too short for PRI (%d).\n", p[0]);
			return;
		}
		if (!(p[1] & 0x20))
		{
			PERROR("basic channel ID with PRI interface.\n");
			return;
		}
		if ((p[1]&0x03) == 0x00)
		{
			/* no channel */
			*channel = CHANNEL_NO;
			return;
		}
		if ((p[1]&0x03) == 0x03)
		{
			/* any channel */
			*channel = CHANNEL_ANY;
			return;
		}
		if (p[0] < 3)
		{
			PERROR("%s: ERROR: IE too short for PRI with channel(%d).\n", __FUNCTION__, p[0]);
			return;
		}
		if (p[2] & 0x10)
		{
			PERROR("channel map not supported.\n");
			return;
		}
		*channel = p[3] & 0x7f;
		if (*channel<1 | *channel==16)
		{
			PERROR("PRI interface channel out of range (%d).\n", *channel);
			return;
		}
	}

	add_trace("channel_id", "exclusive", "%d", *exclusive);
	add_trace("channel_id", "channel", "%d", *channel);
}


/* IE_DATE */
void Pdss1::enc_ie_date(unsigned char **ntmode, msg_t *msg, time_t ti, int no_seconds)
{
	unsigned char *p;
	Q931_info_t *qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);
	int l;

	struct tm *tm;

	tm = localtime(&ti);
	if (!tm)
	{
		PERROR("localtime() returned NULL.\n");
		return;
	}

	add_trace("date", "day", "%d.%d.%d", tm->tm_mday, tm->tm_mon+1, tm->tm_year%100);
	add_trace("date", "time", "%d:%d:%d", tm->tm_hour, tm->tm_min, tm->tm_sec);

	l = 5 + (!no_seconds);
	p = msg_put(msg, l+2);
	if (p_m_d_ntmode)
		*ntmode = p+1;
	else
		qi->date.off = p - (unsigned char *)qi - sizeof(Q931_info_t);
	p[0] = IE_DATE;
	p[1] = l;
	p[2] = tm->tm_year % 100;
	p[3] = tm->tm_mon + 1;
	p[4] = tm->tm_mday;
	p[5] = tm->tm_hour;
	p[6] = tm->tm_min;
	if (!no_seconds)
		p[7] = tm->tm_sec;
}


/* IE_DISPLAY */
void Pdss1::enc_ie_display(unsigned char **ntmode, msg_t *msg, unsigned char *display)
{
	unsigned char *p;
	Q931_info_t *qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);
	int l;

	if (!display[0])
	{
		PERROR("display text not given.\n");
		return;
	}

	if (strlen((char *)display) > 80)
	{
		PERROR("display text too long (max 80 chars), cutting.\n");
		display[80] = '\0';
	}

	add_trace("display", NULL, "%s", display);

	l = strlen((char *)display);
	p = msg_put(msg, l+2);
	if (p_m_d_ntmode)
		*ntmode = p+1;
	else
		qi->display.off = p - (unsigned char *)qi - sizeof(Q931_info_t);
	p[0] = IE_DISPLAY;
	p[1] = l;
	UNCPY((char *)p+2, (char *)display, strlen((char *)display));
}

void Pdss1::dec_ie_display(unsigned char *p, Q931_info_t *qi, unsigned char *display, int display_len)
{
	*display = '\0';

	if (!p_m_d_ntmode)
	{
		p = NULL;
		if (qi->display.off)
			p = (unsigned char *)qi + sizeof(Q931_info_t) + qi->display.off + 1;
	}
	if (!p)
		return;
	if (p[0] < 1)
	{
		PERROR("IE too short (%d).\n", p[0]);
		return;
	}

	strnncpy(display, p+1, p[0], display_len);

	add_trace("display", NULL, "%s", display);
}


/* IE_KEYPAD */
void Pdss1::enc_ie_keypad(unsigned char **ntmode, msg_t *msg, unsigned char *keypad)
{
	unsigned char *p;
	Q931_info_t *qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);
	int l;

	if (!keypad[0])
	{
		PERROR("keypad info not given.\n");
		return;
	}

	add_trace("keypad", NULL, "%s", keypad);

	l = strlen((char *)keypad);
	p = msg_put(msg, l+2);
	if (p_m_d_ntmode)
		*ntmode = p+1;
	else
		qi->keypad.off = p - (unsigned char *)qi - sizeof(Q931_info_t);
	p[0] = IE_KEYPAD;
	p[1] = l;
	UNCPY((char *)p+2, (char *)keypad, strlen((char *)keypad));
}

void Pdss1::dec_ie_keypad(unsigned char *p, Q931_info_t *qi, unsigned char *keypad, int keypad_len)
{
	*keypad = '\0';

	if (!p_m_d_ntmode)
	{
		p = NULL;
		if (qi->keypad.off)
			p = (unsigned char *)qi + sizeof(Q931_info_t) + qi->keypad.off + 1;
	}
	if (!p)
		return;
	if (p[0] < 1)
	{
		PERROR("IE too short (%d).\n", p[0]);
		return;
	}

	strnncpy(keypad, p+1, p[0], keypad_len);

	add_trace("keypad", NULL, "%s", keypad);
}


/* IE_NOTIFY */
void Pdss1::enc_ie_notify(unsigned char **ntmode, msg_t *msg, int notify)
{
	unsigned char *p;
	Q931_info_t *qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);
	int l;

	if (notify<0 || notify>0x7f)
	{
		PERROR("notify(%d) is out of range.\n", notify);
		return;
	}

	add_trace("notify", NULL, "%d", notify);

	l = 1;
	p = msg_put(msg, l+2);
	if (p_m_d_ntmode)
		*ntmode = p+1;
	else
		qi->notify.off = p - (unsigned char *)qi - sizeof(Q931_info_t);
	p[0] = IE_NOTIFY;
	p[1] = l;
	p[2] = 0x80 + notify;
}

void Pdss1::dec_ie_notify(unsigned char *p, Q931_info_t *qi, int *notify)
{
	*notify = -1;

	if (!p_m_d_ntmode)
	{
		p = NULL;
		if (qi->notify.off)
			p = (unsigned char *)qi + sizeof(Q931_info_t) + qi->notify.off + 1;
	}
	if (!p)
		return;
	if (p[0] < 1)
	{
		PERROR("IE too short (%d).\n", p[0]);
		return;
	}

	*notify = p[1] & 0x7f;

	add_trace("notify", NULL, "%d", *notify);
}


/* IE_PROGRESS */
void Pdss1::enc_ie_progress(unsigned char **ntmode, msg_t *msg, int coding, int location, int progress)
{
	unsigned char *p;
	Q931_info_t *qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);
	int l;

	if (coding<0 || coding>0x03)
	{
		PERROR("coding(%d) is out of range.\n", coding);
		return;
	}
	if (location<0 || location>0x0f)
	{
		PERROR("location(%d) is out of range.\n", location);
		return;
	}
	if (progress<0 || progress>0x7f)
	{
		PERROR("progress(%d) is out of range.\n", progress);
		return;
	}

	add_trace("progress", "codeing", "%d", coding);
	add_trace("progress", "location", "%d", location);
	add_trace("progress", "indicator", "%d", progress);

	l = 2;
	p = msg_put(msg, l+2);
	if (p_m_d_ntmode)
		*ntmode = p+1;
	else
		qi->progress.off = p - (unsigned char *)qi - sizeof(Q931_info_t);
	p[0] = IE_PROGRESS;
	p[1] = l;
	p[2] = 0x80 + (coding<<5) + location;
	p[3] = 0x80 + progress;
}

void Pdss1::dec_ie_progress(unsigned char *p, Q931_info_t *qi, int *coding, int *location, int *progress)
{
	*coding = -1;
	*location = -1;
	*progress = -1;

	if (!p_m_d_ntmode)
	{
		p = NULL;
		if (qi->progress.off)
			p = (unsigned char *)qi + sizeof(Q931_info_t) + qi->progress.off + 1;
	}
	if (!p)
		return;
	if (p[0] < 1)
	{
		PERROR("IE too short (%d).\n", p[0]);
		return;
	}

	*coding = (p[1]&0x60) >> 5;
	*location = p[1] & 0x0f;
	*progress = p[2] & 0x7f;

	add_trace("progress", "codeing", "%d", *coding);
	add_trace("progress", "location", "%d", *location);
	add_trace("progress", "indicator", "%d", *progress);
}


/* IE_REDIR_NR (redirecting = during MT_SETUP) */
void Pdss1::enc_ie_redir_nr(unsigned char **ntmode, msg_t *msg, int type, int plan, int present, int screen, int reason, unsigned char *number)
{
	unsigned char *p;
	Q931_info_t *qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);
	int l;

	if (type<0 || type>7)
	{
		PERROR("type(%d) is out of range.\n", type);
		return;
	}
	if (plan<0 || plan>15)
	{
		PERROR("plan(%d) is out of range.\n", plan);
		return;
	}
	if (present > 3)
	{
		PERROR("present(%d) is out of range.\n", present);
		return;
	}
	if (present >= 0) if (screen<0 || screen>3)
	{
		PERROR("screen(%d) is out of range.\n", screen);
		return;
	}
	if (reason > 0x0f)
	{
		PERROR("reason(%d) is out of range.\n", reason);
		return;
	}

	add_trace("redir'ing", "type", "%d", type);
	add_trace("redir'ing", "plan", "%d", plan);
	add_trace("redir'ing", "present", "%d", present);
	add_trace("redir'ing", "screen", "%d", screen);
	add_trace("redir'ing", "reason", "%d", reason);
	add_trace("redir'ing", "number", "%s", number);

	l = 1;
	if (number)
		l += strlen((char *)number);
	if (present >= 0)
	{
		l += 1;
		if (reason >= 0)
			l += 1;
	}
	p = msg_put(msg, l+2);
	if (p_m_d_ntmode)
		*ntmode = p+1;
	else
		qi->redirect_nr.off = p - (unsigned char *)qi - sizeof(Q931_info_t);
	p[0] = IE_REDIR_NR;
	p[1] = l;
	if (present >= 0)
	{
		if (reason >= 0)
		{
			p[2] = 0x00 + (type<<4) + plan;
			p[3] = 0x00 + (present<<5) + screen;
			p[4] = 0x80 + reason;
			if (number)
				UNCPY((char *)p+5, (char *)number, strlen((char *)number));
		} else
		{
			p[2] = 0x00 + (type<<4) + plan;
			p[3] = 0x80 + (present<<5) + screen;
			if (number)
				UNCPY((char *)p+4, (char *)number, strlen((char *)number));
		}
	} else
	{
		p[2] = 0x80 + (type<<4) + plan;
		if (number) if (number[0])
			UNCPY((char *)p+3, (char *)number, strlen((char *)number));
	}
}

void Pdss1::dec_ie_redir_nr(unsigned char *p, Q931_info_t *qi, int *type, int *plan, int *present, int *screen, int *reason, unsigned char *number, int number_len)
{
	*type = -1;
	*plan = -1;
	*present = -1;
	*screen = -1;
	*reason = -1;
	*number = '\0';

	if (!p_m_d_ntmode)
	{
		p = NULL;
		if (qi->redirect_nr.off)
			p = (unsigned char *)qi + sizeof(Q931_info_t) + qi->redirect_nr.off + 1;
	}
	if (!p)
		return;
	if (p[0] < 1)
	{
		PERROR("IE too short (%d).\n", p[0]);
		return;
	}

	*type = (p[1]&0x70) >> 4;
	*plan = p[1] & 0xf;
	if (!(p[1] & 0x80))
	{
		*present = (p[2]&0x60) >> 5;
		*screen = p[2] & 0x3;
		if (!(p[2] & 0x80))
		{
			*reason = p[3] & 0x0f;
			strnncpy(number, p+4, p[0]-3, number_len);
		} else
		{
			strnncpy(number, p+3, p[0]-2, number_len);
		}
	} else
	{
		strnncpy(number, p+2, p[0]-1, number_len);
	}

	add_trace("redir'ing", "type", "%d", *type);
	add_trace("redir'ing", "plan", "%d", *plan);
	add_trace("redir'ing", "present", "%d", *present);
	add_trace("redir'ing", "screen", "%d", *screen);
	add_trace("redir'ing", "reason", "%d", *reason);
	add_trace("redir'ing", "number", "%s", number);
}


/* IE_REDIR_DN (redirection = during MT_NOTIFY) */
void Pdss1::enc_ie_redir_dn(unsigned char **ntmode, msg_t *msg, int type, int plan, int present, unsigned char *number)
{
	unsigned char *p;
	Q931_info_t *qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);
	int l;

	if (type<0 || type>7)
	{
		PERROR("type(%d) is out of range.\n", type);
		return;
	}
	if (plan<0 || plan>15)
	{
		PERROR("plan(%d) is out of range.\n", plan);
		return;
	}
	if (present > 3)
	{
		PERROR("present(%d) is out of range.\n", present);
		return;
	}

	add_trace("redir'tion", "type", "%d", type);
	add_trace("redir'tion", "plan", "%d", plan);
	add_trace("redir'tion", "present", "%d", present);
	add_trace("redir'tion", "number", "%s", number);

	l = 1;
	if (number)
		l += strlen((char *)number);
	if (present >= 0)
		l += 1;
	p = msg_put(msg, l+2);
	if (p_m_d_ntmode)
		*ntmode = p+1;
	else
		qi->redirect_dn.off = p - (unsigned char *)qi - sizeof(Q931_info_t);
	p[0] = IE_REDIR_DN;
	p[1] = l;
	if (present >= 0)
	{
		p[2] = 0x00 + (type<<4) + plan;
		p[3] = 0x80 + (present<<5);
		if (number)
			UNCPY((char *)p+4, (char *)number, strlen((char *)number));
	} else
	{
		p[2] = 0x80 + (type<<4) + plan;
		if (number)
			UNCPY((char *)p+3, (char *)number, strlen((char *)number));
	}
}

void Pdss1::dec_ie_redir_dn(unsigned char *p, Q931_info_t *qi, int *type, int *plan, int *present, unsigned char *number, int number_len)
{
	*type = -1;
	*plan = -1;
	*present = -1;
	*number = '\0';

	if (!p_m_d_ntmode)
	{
		p = NULL;
		if (qi->redirect_dn.off)
			p = (unsigned char *)qi + sizeof(Q931_info_t) + qi->redirect_dn.off + 1;
	}
	if (!p)
		return;
	if (p[0] < 1)
	{
		PERROR("IE too short (%d).\n", p[0]);
		return;
	}

	*type = (p[1]&0x70) >> 4;
	*plan = p[1] & 0xf;
	if (!(p[1] & 0x80))
	{
		*present = (p[2]&0x60) >> 5;
		strnncpy(number, p+3, p[0]-2, number_len);
	} else
	{
		strnncpy(number, p+2, p[0]-1, number_len);
	}

	add_trace("redir'tion", "type", "%d", *type);
	add_trace("redir'tion", "plan", "%d", *plan);
	add_trace("redir'tion", "present", "%d", *present);
	add_trace("redir'tion", "number", "%s", number);
}


/* IE_FACILITY */
void Pdss1::enc_ie_facility(unsigned char **ntmode, msg_t *msg, unsigned char *facility, int facility_len)
{
	unsigned char *p;
	Q931_info_t *qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);
	int l;

	char buffer[768];
	int i;

	if (!facility || facility_len<=0)
	{
		return;
	}

	i = 0;
	while(i < facility_len)
	{
		UPRINT(buffer+(i*3), " %02x", facility[i]);
		i++;
	}
		
	add_trace("facility", NULL, "%s", buffer+1);

	l = facility_len;
	p = msg_put(msg, l+2);
	if (p_m_d_ntmode)
		*ntmode = p+1;
	else
		qi->facility.off = p - (unsigned char *)qi - sizeof(Q931_info_t);
	p[0] = IE_FACILITY;
	p[1] = l;
	memcpy(p+2, facility, facility_len);
}

void Pdss1::dec_ie_facility(unsigned char *p, Q931_info_t *qi, unsigned char *facility, int *facility_len)
{
	char debug[768];
	int i;

	*facility_len = 0;

	if (!p_m_d_ntmode)
	{
		p = NULL;
		if (qi->facility.off)
			p = (unsigned char *)qi + sizeof(Q931_info_t) + qi->facility.off + 1;
	}
	if (!p)
		return;

	*facility_len = p[0];
	memcpy(facility, p+1, *facility_len);

	i = 0;
	while(i < *facility_len)
	{
		UPRINT(debug+(i*3), " %02x", facility[i]);
		i++;
	}
	debug[i*3] = '\0';
		
	add_trace("facility", NULL, "%s", debug[0]?debug+1:"<none>");
}


/* facility for siemens CENTEX (known parts implemented only) */
void Pdss1::enc_facility_centrex(unsigned char **ntmode, msg_t *msg, unsigned char *cnip, int setup)
{
	unsigned char centrex[256];
	int i = 0;

	if (!cnip)
		return;

	/* centrex facility */
	centrex[i++] = CENTREX_FAC;
	centrex[i++] = CENTREX_ID;

	/* cnip */
	if (strlen((char *)cnip) > 15)
	{
		PDEBUG(DEBUG_PORT, "%s: CNIP/CONP text too long (max 13 chars), cutting.\n");
		cnip[15] = '\0';
	}
	// dunno what the 8 bytes mean
	if (setup)
	{
		centrex[i++] = 0x17;
		centrex[i++] = 0x02;
		centrex[i++] = 0x02;
		centrex[i++] = 0x44;
		centrex[i++] = 0x18;
		centrex[i++] = 0x02;
		centrex[i++] = 0x01;
		centrex[i++] = 0x09;
	} else
	{
		centrex[i++] = 0x18;
		centrex[i++] = 0x02;
		centrex[i++] = 0x02;
		centrex[i++] = 0x81;
		centrex[i++] = 0x09;
		centrex[i++] = 0x02;
		centrex[i++] = 0x01;
		centrex[i++] = 0x0a;
	}

	centrex[i++] = 0x80;
	centrex[i++] = strlen((char *)cnip);
	UCPY((char *)(&centrex[i]), (char *)cnip);
	i += strlen((char *)cnip);
	add_trace("facility", "cnip", "%s", cnip);

	/* encode facility */
	enc_ie_facility(ntmode, msg, centrex, i);
}

void Pdss1::dec_facility_centrex(unsigned char *p, Q931_info_t *qi, unsigned char *cnip, int cnip_len)
{
	unsigned char centrex[256];
	char debug[768];
	int facility_len = 0;
	int i = 0, j;
	*cnip = '\0';

	dec_ie_facility(p, qi, centrex, &facility_len);
	if (facility_len >= 2)
	{
		if (centrex[i++] != CENTREX_FAC)
			return;
		if (centrex[i++] != CENTREX_ID)
			return;
	}

	/* loop sub IEs of facility */
	while(facility_len > i+1)
	{
		if (centrex[i+1]+i+1 > facility_len)
		{
			PERROR("short read of centrex facility.\n");
			return;
		}
		switch(centrex[i])
		{
			case 0x80:
			strnncpy(cnip, &centrex[i+2], centrex[i+1], cnip_len);
			add_trace("facility", "cnip", "%s", cnip);
			break;

			default:
			j = 0;
			while(j < centrex[i+1])
			{
				UPRINT(debug+(j*3), " %02x", centrex[i+1+j]);
				i++;
			}
			add_trace("facility", "CENTREX", "unknown=0x%2x len=%d%s\n", centrex[i], centrex[i+1], debug);
		}
		i += 1+centrex[i+1];
	}
}


/* IE_USERUSER */
void Pdss1::enc_ie_useruser(unsigned char **ntmode, msg_t *msg, int protocol, unsigned char *user, int user_len)
{
	unsigned char *p;
	Q931_info_t *qi = (Q931_info_t *)(msg->data + mISDN_HEADER_LEN);
	int l;

	char buffer[768];
	int i;

	if (protocol<0 || protocol>127)
	{
		PERROR("protocol(%d) is out of range.\n", protocol);
		return;
	}
	if (!user || user_len<=0)
	{
		return;
	}

	i = 0;
	while(i < user_len)
	{
		UPRINT(buffer+(i*3), " %02x", user[i]);
		i++;
	}
		
	add_trace("useruser", "protocol", "%d", protocol);
	add_trace("useruser", "value", "%s", buffer);

	l = user_len;
	p = msg_put(msg, l+3);
	if (p_m_d_ntmode)
		*ntmode = p+1;
	else
		qi->useruser.off = p - (unsigned char *)qi - sizeof(Q931_info_t);
	p[0] = IE_USER_USER;
	p[1] = l;
	p[2] = 0x80 + protocol;
	memcpy(p+3, user, user_len);
}

void Pdss1::dec_ie_useruser(unsigned char *p, Q931_info_t *qi, int *protocol, unsigned char *user, int *user_len)
{
	char buffer[768];
	int i;

	*user_len = 0;
	*protocol = -1;

	if (!p_m_d_ntmode)
	{
		p = NULL;
		if (qi->useruser.off)
			p = (unsigned char *)qi + sizeof(Q931_info_t) + qi->useruser.off + 1;
	}
	if (!p)
		return;

	*user_len = p[0]-1;
	if (p[0] < 1)
		return;
	*protocol = p[1];
	memcpy(user, p+2, (*user_len<=128)?*(user_len):128); /* clip to 128 maximum */

	i = 0;
	while(i < *user_len)
	{
		UPRINT(buffer+(i*3), " %02x", user[i]);
		i++;
	}
	buffer[i*3] = '\0';
		
	add_trace("useruser", "protocol", "%d", *protocol);
	add_trace("useruser", "value", "%s", buffer);
}


