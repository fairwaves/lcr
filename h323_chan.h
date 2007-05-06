///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// PBX4Linux                                                                 //
//                                                                           //
//---------------------------------------------------------------------------//
// Copyright: Andreas Eversberg                                              //
//                                                                           //
// h323_chan header file                                                     //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////


#ifndef H323_CHAN_HEADER
#define H323_CHAN_HEADER

class H323_chan : public PIndirectChannel
{
  public:
	H323_chan(const PString &token, BOOL isEncoding);
	~H323_chan(void);
	BOOL Close(void);
	BOOL IsOpen(void) const;
	BOOL Read(void *buf, PINDEX len);
	BOOL Write(const void *buf, PINDEX len);

  private:
	PString d_token;
	PTime start;
	BOOL transfering;
	PInt64 elapsed;
};

#endif

