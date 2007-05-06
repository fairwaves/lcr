///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// PBX4Linux                                                                 //
//                                                                           //
//---------------------------------------------------------------------------//
// Copyright: Andreas Eversberg                                              //
//                                                                           //
// h323_ep endpoint header file                                              //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////


#ifndef H323_EP_HEADER
#define H323_EP_HEADER   

class H323_con;
class H323_ep : public H323EndPoint
{
	public:
	H323_ep(void);
	~H323_ep();
	H323Connection *CreateConnection(unsigned callReference);  
	void OnConnectionEstablished(H323Connection &connection, const PString &token);
	BOOL OnAlerting(H323Connection &connection, const H323SignalPDU &alertingPDU, const PString &user);
	void OnConnectionCleared(H323Connection &connection, const PString &token);
	BOOL OpenAudioChannel(H323Connection &connection, BOOL isEncoding, unsigned bufferSize, H323AudioCodec &codec);
	BOOL OpenVideoChannel(H323Connection &connection, BOOL isEncoding, H323VideoCodec &codec);
	BOOL Init(void);
	void SetEndpointTypeInfo (H225_EndpointType & info) const;
	BOOL Call(char *token_string, char *caller, char *host);

	private:

};

#endif // H323_EP_HEADER

