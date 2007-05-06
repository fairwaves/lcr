///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// PBX4Linux                                                                 //
//                                                                           //
//---------------------------------------------------------------------------//
// Copyright: Andreas Eversberg                                              //
//                                                                           //
// h323_con connection header file                                           //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////


#ifndef H323_CON_HEADER
#define H323_CON_HEADER   

class H323_chan;
class H323_con : public H323Connection
{
  public:
	
	H323_con(H323_ep &endpoint, unsigned callReference);
	~H323_con();
	AnswerCallResponse OnAnswerCall(const PString &, const H323SignalPDU &, H323SignalPDU &);
	BOOL OnOutgoingCall(const H323SignalPDU &connectPDU);
	BOOL OnSendSignalSetup(H323SignalPDU &);
	BOOL OnStartLogicalChannel(H323Channel &channel);
	void OnUserInputString (const PString &value);
	H323SignalPDU *GetConnectPDU(void);

  private:
};

#endif // H323_CON_HEADER


