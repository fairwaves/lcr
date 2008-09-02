/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** crypt header file                                                         **
**                                                                           **
\*****************************************************************************/ 


enum { /* enpoint's user states */
	CRYPT_OFF,		/* no encryption */
	CRYPT_KWAIT,		/* wait for key-exchange mehtod */
	CRYPT_SWAIT,		/* wait for shared secret method */
	CRYPT_ON,		/* crypting */
	CRYPT_RELEASE,		/* wait for deactivation */
};

#define CM_TO_IDENT	10	/* timeout for identifying remote peer */
#define CM_TO_PUBKEY	60	/* timeout for public key generation */
#define CM_TO_CSKEY	5	/* timeout for crypting session key */

enum { /* crypt manager states */
	CM_ST_NULL,		/* no encryption used */
	CM_ST_IDENT,		/* find the remote pary */
	CM_ST_KEYGEN,		/* generating public/private key */
	CM_ST_KEYWAIT,		/* waiting for public key */
	CM_ST_CSKEY,		/* generate crypted session key */
	CM_ST_CSWAIT,		/* wait for CSKey */
	CM_ST_SESSION,		/* wait for session key decryption */
	CM_ST_WAIT_DELAY,	/* wait sone time until the cskey is transferred */
	CM_ST_WAIT_CRYPT,	/* wait for encryption from session engine */
	CM_ST_ACTIVE,		/* done with encryption */
	CM_ST_RELEASE,		/* wait until key engine has finished, after abort */

	CM_ST_SWAIT,		/* wait for share key establishment */
	CM_ST_SACTIVE,		/* shared key established */
};

#define CM_ST_NAMES \
static const char *cm_st_name[] = { \
	"NULL", \
	"IDENT", \
	"KEYGEN", \
	"KEYWAIT", \
	"CSKEY", \
	"CSWAIT", \
	"SESSION", \
	"WAIT_DELAY", \
	"WAIT_CRYPT", \
	"ACTIVE", \
	"RELEASE", \
	"SWAIT", \
	"SACTIVE", \
}; \
int cm_st_num = sizeof(cm_st_name) / sizeof(char *);

enum { /* messages */
	/* messages to/from peer */
	CP_IDENT,		/* send random number, cpu power */
	CP_SLAVE,		/* tell remote to be slave */
	CP_MASTER,		/* tell remote to be master */
	CP_LOOPED,		/* tell remote (us) that the call is looped */
	CP_PUBKEY,		/* send public key */
	CP_CSKEY,		/* send encrypted session key */
	CP_ABORT,		/* send abort message */
	/* messages to/from "key engine" */
	CK_GENRSA_REQ,		/* generate rsa key */
	CK_GENRSA_CONF,		/* rsa key result */
	CK_CPTRSA_REQ,		/* crypt session key */
	CK_CPTRSA_CONF,		/* session key result */
	CK_DECRSA_REQ,		/* decode session key */
	CK_DECRSA_CONF,		/* session key result */
	CK_ERROR_IND,		/* error of engine */
	/* messages to/from "crypt engine" */
	CC_ACTBF_REQ,		/* activate blowfish */
	CC_ACTBF_CONF,		/* blowfish activated */
	CC_ERROR_IND,		/* failed to activate session encryption */
	CC_DACT_REQ,		/* deactivate session encryption */
	/* messages to/from user interface */
	CU_ACTK_REQ,		/* request encryption with key exchange */
	CU_ACTK_CONF,		/* encryption now active */
	CU_ACTS_REQ,		/* request shared key encryption */
	CU_ACTS_CONF,		/* encryption now active */
	CU_ERROR_IND,		/* encryption failed */
	CU_INFO_REQ,		/* info reques */
	CU_INFO_CONF,		/* info to the user*/
	CU_INFO_IND,		/* info to the user*/
	CU_DACT_REQ,		/* deactivate encryption */
	CU_DACT_CONF,		/* encryption now inactive */
	CU_DACT_IND,		/* encryption now inactive */
	/* messages to/from message transponder */
	CR_LISTEN_REQ,		/* start listening to messages */
	CR_UNLISTEN_REQ,	/* stop listening to messages */
	CR_MESSAGE_REQ,		/* send message */
	CR_MESSAGE_IND,		/* receive message */
	/* messages from ISDN */
	CI_DISCONNECT_IND,	/* call is disconnected */
	/* message timeout */
	CT_TIMEOUT,		/* current state timed out */
};

#define CM_MSG_NAMES \
static const char *cm_msg_name[] = { \
	"CP_IDENT", \
	"CP_SLAVE", \
	"CP_MASTER", \
	"CP_LOOPED", \
	"CP_PUBKEY", \
	"CP_CSKEY", \
	"CP_ABORT", \
	"CK_GENRSA_REQ", \
	"CK_GENRSA_CONF", \
	"CK_CPTRSA_REQ", \
	"CK_CPTRSA_CONF", \
	"CK_DECRSA_REQ", \
	"CK_DECRSA_CONF", \
	"CK_ERROR_IND", \
	"CC_ACTBF_REQ", \
	"CC_ACTBF_CONF", \
	"CC_ERROR_IND", \
	"CC_DACT_REQ", \
	"CU_ACTK_REQ", \
	"CU_ACTK_CONF", \
	"CU_ACTS_REQ", \
	"CU_ACTS_CONF", \
	"CU_ERROR_IND", \
	"CU_INFO_REQ", \
	"CU_INFO_CONF", \
	"CU_INFO_IND", \
	"CU_DACT_REQ", \
	"CU_DACT_CONF", \
	"CU_DACT_IND", \
	"CR_LISTEN_REQ", \
	"CR_UNLISTEN_REQ", \
	"CR_MESSAGE_REQ", \
	"CR_MESSAGE_IND", \
	"CI_DISCONNECT_IND", \
	"CT_TIMEOUT", \
}; \
int cm_msg_num = sizeof(cm_msg_name) / sizeof(char *);

/* peer2peer messages */
#define CMSG_IDENT		0x01
#define CMSG_MASTER		0x11
#define CMSG_SLAVE		0x12
#define CMSG_LOOPED		0x13
#define CMSG_PUBKEY		0x21
#define CMSG_CSKEY		0x22
#define CMSG_ABORT		0x81

/* peer2peer info elements */
#define	CM_INFO_MESSAGE		0x01
#define	CM_INFO_RANDOM		0x10
#define	CM_INFO_BOGOMIPS	0x11
#define	CM_INFO_PUBKEY		0x12
#define	CM_INFO_PUBEXPONENT	0x13
#define	CM_INFO_CSKEY		0x14


#define CM_ADDINF(a, b, c) \
	cryptman_addinf(buf, sizeof(buf), a, b, c);
#define CM_SIZEOFINF(a) \
	cryptman_sizeofinf(param, a);
#define CM_GETINF(a, b) \
	cryptman_getinf(param, a, b);

void crc_init(void);
unsigned int crc32(unsigned char *data, int len);
int cryptman_encode_bch(unsigned char *data, int len, unsigned char *buf, int buf_len);

