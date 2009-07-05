/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** cryption stuff                                                            **
**                                                                           **
\*****************************************************************************/ 

/* how authentication is performed:

Alice                                                         Bob
-----------------------------------------------------------------
                         unencrypted call
start        *
	     | -----> sending random number -----> (gets lost)
             |
             |                                     *        start
comparing    | <----- sending random number <----- |
master state |                                     |
             | -----> sending "slave" code  -----> |  slave state
calculating  |                                     |
rsa key pair |                                     |
             |                                     |
done         | -----> sending public key    -----> |
             |                                     |     crpyting
             |                                     |  session key
             |                                     |
             | <----- sending session key   <----- |         done
decrypting   |                                     | enable crypt
dession key  |                                     *         stop
             |
done         |
enable crypt |
stop         *
                         encrypted call

When Bob and Alice activate their authentication process, they will make
up a random number.

Lets assume Alice starts encryption first. She activates the authentication
process. Bob hat not activates the process yet. Alice sends a random number
to Bob, but he will ignore it, because he is not listening.

Bob also activates the authentication process, and sends his random number.
Now Alice will receive that number and compares the values. If the values
are equal, the process will fail and Alice will send the message "LOOPED".
If Alice's random number is greater, she will identify herself as "master".
Bob will get a "SLAVE" message. Bob might also send a "MASTER" message,
if he got Alice's random number, due to parallel activation of the
authentication.

After transmission or reception of a master message, more messages are
ignored. After reception of a "RANDOM" message, more messages are
ignored. A reception of a "RANDOM" message always causes to identify who
is slave.

Now Alice starts calculating his public/private key pair, because she is
"master". When Bob receives the "SLAVE" message, he will change the timeout
value. If no random number or "SLAVE", "MASTER" or "LOOPED" is received within
a timeout value, the "ABORT" message is sent. If the "ABORT" message is
received at any state of the process, the process is aborted.

After the key of Alices is calculated, she will send it to Bob. Bob will use
the key to start encryption of a random session key. Both will change their
timeout values.

After Bob has finished is crypted session key, he will send it to Alice and
enable encryption. Bob has finished his process now.

As soon as Alice received the encrypted key, she will encrypt it and also
enable encryption with the same key as Bob. Alis has finished her process now.

Both will get displayed some of the first digits of the public key. Both can
talk about the digits now. A man in the middle cannot change the keys without
changing the public key. The voice of Alice and/or Bob is used to "sign" and
"check" that the public key is not modified.

If Alice or Bob wants to stop encryption, one will send the "ABORT" message.
After transmission or reception.


The states of the process:

CM_ST_NULL
----------
no encryption

CM_ST_IDENT
-----------
Waiting for the remote random number or "MASTER", "SLAVE", "LOOPED" message.

CM_ST_KEYGEN
------------
The master generates the key and waits for the key engine to finish.

CM_ST_KEYWAIT
-------------
The slave waits for the master to send the key.

CM_ST_CSKEY
-----------
The slave generates the session key and waits for the encryption engine to
finish.

CM_ST_CSWAIT
------------
The master waits for the slave to send the crypted session key.

CM_ST_DSKEY
-----------
The master waits for the decryption engine to finish decryption of the session
key.

CM_ST_ACTIVE
------------
The encryption is established.


Timouts
-------
CM_TO_IDENT	= waiting for the remote party to enable encryption
CM_TO_KEY	= waiting for key generation
CM_TO_CSKEY	= waiting for session key encryption
CM_TO_DSKEY	= waiting for session key decryption


Structure of message:
---------------------

one octet message element
two octets element length (first octet = high-byte)
data as given in length

last element is 0
the message type is encoded as element


*/

#include "main.h"
#ifdef CRYPTO
#include <openssl/rsa.h>
#endif


/* convert key string to binary key vector
 * returns 0 if an error ocurred
 */
unsigned char *crypt_key(unsigned char *key, int *binary_len)
{
	static unsigned char binary_key[2048];
	int i = 0;

	binary_key[0] = '\0';

	if (!key[0])
		return(NULL);

	/* check for 0xXXXX... type of key */
	if (!strncmp((char *)key, "0x", 2)) {
		key+=2;
		while(*key) {
			if (i == (int)sizeof(binary_key))
				return(NULL);

			if (*key>='0' && *key<='9')
				binary_key[i] = (*key-'0') << 8;
			else if (*key>='a' && *key<='f')
				binary_key[i] = (*key-'a'+10) << 8;
			else if (*key>='A' && *key<='F')
				binary_key[i] = (*key-'A'+10) << 8;
			else
				return(NULL);
			key++;

			if (*key>='0' && *key<='9')
				binary_key[i] += (*key - '0');
			else if (*key>='a' && *key<='f')
				binary_key[i] += (*key - 'a' + 10);
			else if (*key>='A' && *key<='F')
				binary_key[i] += (*key - 'A' + 10);
			else
				return(NULL);
			key++;

			i++;
		}
		*binary_len = i;
		return(binary_key);
	}

	/* ascii key too long */
	if (strlen((char *)key) >= sizeof((char *)binary_key))
		return(NULL);

	memcpy(binary_key, key, strlen((char *)key));
	*binary_len = strlen((char *)key);
	return(binary_key);
}

/*
 * support routine to get cpu speed
 */
static unsigned int get_bogomips(void)
{
	FILE *fp;
	char buffer[64], *p;

	fp = fopen("/proc/cpuinfo", "r");
	if (!fp) {
		PERROR("Cannot access /proc/cpuinfo. Will not use cpuinfo for identification of pear\n");
		return(0);
	}
	fduse++;
	buffer[sizeof(buffer-1)] = '\0';
	while(fgets(buffer, sizeof(buffer)-1, fp)) {
		if (!!strncmp(buffer, "bogomips", 8))
			continue;
		if (!strchr(buffer, ':'))
			continue;
		p = strchr(buffer, ':')+1;
		while(*p == ' ')
			p++;
		if (strchr(p, '.'))
			*strchr(p, '.') = '\0';
		fclose(fp);
		fduse--;
		return(atoi(p));
	}
	fclose(fp);
	fduse--;
	PERROR("Cannot find 'bogomips' in /proc/cpuinfo. Will not use cpuinfo for identification of pear\n");
	return(0);
}


/*
 * crc 32 stuff
 */
static unsigned int crc_reflect(unsigned int ref, char ch)
{
	unsigned int value = 0;
	int i;

	i = 1;
	while(i < ch+1) {
		if(ref & 1)
			value |= 1 << (ch - i);
		ref >>= 1;
		i++;
	}
	return(value);
}

static unsigned int crc32_table[256];
static int crc_initialized = 0;

void crc_init(void)
{
	unsigned int ulPolynomial = 0x04c11db7;
	int i, j;

	i = 0;
	while(i < 256) {
		crc32_table[i] = crc_reflect(i, 8) << 24;
		j = 0;
		while(j < 8) {
			crc32_table[i] = (crc32_table[i] << 1) ^ (crc32_table[i] & (1 << 31) ? ulPolynomial : 0);
			j++;
		}
		crc32_table[i] = crc_reflect(crc32_table[i], 32);
		i++;
	}
	crc_initialized = 1;
}

unsigned int crc32(unsigned char *data, int len)
{
	unsigned int crc = 0xffffffff;

	if (!crc_initialized)
		FATAL("crc not initialized, exitting...");

	while (len--)
		crc = (crc >> 8) ^ crc32_table[(crc & 0xFF) ^ *data++];
	return(crc^0xffffffff);
}


CM_ST_NAMES

/* give name of state */
static const char *statename(int state)
{
	if (state>=0 && state<cm_st_num)
		return(cm_st_name[state]);
	return("<<STATE UNKNOWN>>");
}

/*
 * authentication key generation, encryption, decryption
 */
struct auth_args {
	class EndpointAppPBX *apppbx;
	int	job;
};

static void *keyengine_child(void *arg)
{
	struct auth_args *args = (struct auth_args *)arg;
	class EndpointAppPBX *apppbx = args->apppbx;
	int job = args->job;
#ifdef CRYPTO
	RSA *rsa;
	int exponent;
	int i;
#endif

	struct sched_param schedp;
	int ret;

	PDEBUG((DEBUG_EPOINT | DEBUG_CRYPT), "EPOINT(%d) child process started for using libcrypto\n", apppbx->ea_endpoint->ep_serial);

	/* lower priority to keep pbx running fluently */
	if (options.schedule > 0) {
		memset(&schedp, 0, sizeof(schedp));
		schedp.sched_priority = 0;
		ret = sched_setscheduler(0, SCHED_OTHER, &schedp);
		if (ret < 0) {
			PERROR("Scheduling to normal priority failed (errno = %d).\nExitting child process...\n", errno);
			goto done;
		}
	}

	switch(job) {
		/* generate rsa key pair */
		case CK_GENRSA_REQ:
#ifndef CRYPTO
		PERROR("Not compliled wiht crypto.\n");
		apppbx->e_crypt_keyengine_return = -1;
#else
		srandom(*((unsigned int *)mISDN_rand) ^ random());
//		exponent = (((random()<<1)|1) & 0x7f) + 0x80; /* odd */
		exponent = 65537;
//		if (exponent < 3) exponent = 3; /* >= 3 */
		rsa = RSA_generate_key(RSA_BITS, exponent, NULL, NULL);
		if (!rsa) {
			PERROR("Failed to generate rsa key pair.\n");
			apppbx->e_crypt_keyengine_return = -1;
			break;
		}
		ememuse++;
		apppbx->e_crypt_rsa_n_len = BN_num_bytes(rsa->n);
		if (apppbx->e_crypt_rsa_n_len > (int)sizeof(apppbx->e_crypt_rsa_n)) {
			kerror_buffer:
			PERROR("e_crypt_rsa_* too small for bignum.\n");
			apppbx->e_crypt_keyengine_return = -1;
			RSA_free(rsa);
			ememuse--;
			break;
		}
		BN_bn2bin(rsa->n, apppbx->e_crypt_rsa_n);
		apppbx->e_crypt_rsa_n_len = BN_num_bytes(rsa->n);
		if (apppbx->e_crypt_rsa_e_len > (int)sizeof(apppbx->e_crypt_rsa_e))
			goto kerror_buffer;
		BN_bn2bin(rsa->e, apppbx->e_crypt_rsa_e);
		apppbx->e_crypt_rsa_e_len = BN_num_bytes(rsa->e);
		if (apppbx->e_crypt_rsa_d_len > (int)sizeof(apppbx->e_crypt_rsa_d))
			goto kerror_buffer;
		BN_bn2bin(rsa->d, apppbx->e_crypt_rsa_d);
		apppbx->e_crypt_rsa_p_len = BN_num_bytes(rsa->p);
		if (apppbx->e_crypt_rsa_p_len > (int)sizeof(apppbx->e_crypt_rsa_p))
			goto kerror_buffer;
		BN_bn2bin(rsa->p, apppbx->e_crypt_rsa_p);
		apppbx->e_crypt_rsa_q_len = BN_num_bytes(rsa->q);
		if (apppbx->e_crypt_rsa_q_len > (int)sizeof(apppbx->e_crypt_rsa_q))
			goto kerror_buffer;
		BN_bn2bin(rsa->q, apppbx->e_crypt_rsa_q);
		apppbx->e_crypt_rsa_dmp1_len = BN_num_bytes(rsa->dmp1);
		if (apppbx->e_crypt_rsa_dmp1_len > (int)sizeof(apppbx->e_crypt_rsa_dmp1))
			goto kerror_buffer;
		BN_bn2bin(rsa->dmp1, apppbx->e_crypt_rsa_dmp1);
		apppbx->e_crypt_rsa_dmq1_len = BN_num_bytes(rsa->dmq1);
		if (apppbx->e_crypt_rsa_dmq1_len > (int)sizeof(apppbx->e_crypt_rsa_dmq1))
			goto kerror_buffer;
		BN_bn2bin(rsa->dmq1, apppbx->e_crypt_rsa_dmq1);
		apppbx->e_crypt_rsa_iqmp_len = BN_num_bytes(rsa->iqmp);
		if (apppbx->e_crypt_rsa_iqmp_len > (int)sizeof(apppbx->e_crypt_rsa_iqmp))
			goto kerror_buffer;
		BN_bn2bin(rsa->iqmp, apppbx->e_crypt_rsa_iqmp);
		PDEBUG(DEBUG_CRYPT, "gen: rsa n=%02x...\n", *apppbx->e_crypt_rsa_n);
		PDEBUG(DEBUG_CRYPT, "gen: rsa e=%02x...\n", *apppbx->e_crypt_rsa_e);
		PDEBUG(DEBUG_CRYPT, "gen: rsa d=%02x...\n", *apppbx->e_crypt_rsa_d);
		PDEBUG(DEBUG_CRYPT, "gen: rsa p=%02x...\n", *apppbx->e_crypt_rsa_p);
		PDEBUG(DEBUG_CRYPT, "gen: rsa q=%02x...\n", *apppbx->e_crypt_rsa_q);
		PDEBUG(DEBUG_CRYPT, "gen: rsa dmp1=%02x...\n", *apppbx->e_crypt_rsa_dmp1);
		PDEBUG(DEBUG_CRYPT, "gen: rsa dmq1=%02x...\n", *apppbx->e_crypt_rsa_dmq1);
		PDEBUG(DEBUG_CRYPT, "gen: rsa iqmp=%02x...\n", *apppbx->e_crypt_rsa_iqmp);
		apppbx->e_crypt_keyengine_return = 1;
		RSA_free(rsa);
		ememuse--;
#endif
		break;

		/* encrypt session key */
		case CK_CPTRSA_REQ:
#ifndef CRYPTO
		PERROR("No crypto lib.\n");
		apppbx->e_crypt_keyengine_return = -1;
#else
		/* generating session key */
		srandom(*((unsigned int *)mISDN_rand) ^ random());
		i = 0;
		while(i < 56) {
			apppbx->e_crypt_key[i] = random();
			apppbx->e_crypt_key[i] ^= mISDN_rand[random() & 0xff];
			i++;
		}
		apppbx->e_crypt_key_len = i;
		/* encrypt via rsa */
		rsa = RSA_new();
		if (!rsa) {
			PERROR("Failed to allocate rsa structure.\n");
			apppbx->e_crypt_keyengine_return = 1;
			break;
		}
		ememuse++;
		rsa->n = BN_new();
		rsa->e = BN_new();
		if (!rsa->n || !rsa->e) {
			PERROR("Failed to generate rsa structure.\n");
			apppbx->e_crypt_keyengine_return = -1;
			RSA_free(rsa);
			ememuse--;
			break;
		}
		if (!BN_bin2bn(apppbx->e_crypt_rsa_n, apppbx->e_crypt_rsa_n_len, rsa->n)) {
			eerror_bin2bn:
			PERROR("Failed to convert binary to bignum.\n");
			apppbx->e_crypt_keyengine_return = -1;
			RSA_free(rsa);
			ememuse--;
			break;
		}
		if ((apppbx->e_crypt_rsa_n_len*8) != BN_num_bits(rsa->n)) {
			PERROR("SOFTWARE API ERROR: length not equal stored data. (%d != %d)\n", apppbx->e_crypt_rsa_n_len*8, BN_num_bits(rsa->n));
			apppbx->e_crypt_keyengine_return = -1;
			RSA_free(rsa);
			ememuse--;
			break;
		}
		if (!BN_bin2bn(apppbx->e_crypt_rsa_e, apppbx->e_crypt_rsa_e_len, rsa->e))
			goto eerror_bin2bn;
		PDEBUG(DEBUG_CRYPT, "crypt: rsa n=%02x...\n", *apppbx->e_crypt_rsa_n);
		PDEBUG(DEBUG_CRYPT, "crypt: rsa e=%02x...\n", *apppbx->e_crypt_rsa_e);
		PDEBUG(DEBUG_CRYPT, "crypt: key =%02x%02x%02x%02x... (len=%d)\n", apppbx->e_crypt_key[0], apppbx->e_crypt_key[1], apppbx->e_crypt_key[2], apppbx->e_crypt_key[3], apppbx->e_crypt_key_len);
		apppbx->e_crypt_ckey_len = RSA_public_encrypt(
			apppbx->e_crypt_key_len,
			apppbx->e_crypt_key,
			apppbx->e_crypt_ckey,
			rsa,
			RSA_PKCS1_PADDING);
		PDEBUG(DEBUG_CRYPT, "crypt: ckey =%02x%02x%02x%02x... (len=%d)\n", apppbx->e_crypt_ckey[0], apppbx->e_crypt_ckey[1], apppbx->e_crypt_ckey[2], apppbx->e_crypt_ckey[3], apppbx->e_crypt_ckey_len);
		RSA_free(rsa);
		ememuse--;
		if (apppbx->e_crypt_ckey_len > 0)
			apppbx->e_crypt_keyengine_return = 1;
		else
			apppbx->e_crypt_keyengine_return = -1;
#endif
		break;

		/* decrypt session key */
		case CK_DECRSA_REQ:
#ifndef CRYPTO
		PERROR("No crypto lib.\n");
		apppbx->e_crypt_keyengine_return = -1;
#else
		rsa = RSA_new();
		if (!rsa) {
			PERROR("Failed to allocate rsa structure.\n");
			apppbx->e_crypt_keyengine_return = 1;
			break;
		}
		ememuse++;
		rsa->n = BN_new();
		rsa->e = BN_new();
		rsa->d = BN_new();
		rsa->p = BN_new();
		rsa->q = BN_new();
		rsa->dmp1 = BN_new();
		rsa->dmq1 = BN_new();
		rsa->iqmp = BN_new();
		if (!rsa->n || !rsa->e
		 || !rsa->d || !rsa->p
		 || !rsa->q || !rsa->dmp1
		 || !rsa->dmq1 || !rsa->iqmp) {
			PERROR("Failed to generate rsa structure.\n");
			apppbx->e_crypt_keyengine_return = 1;
			RSA_free(rsa);
			ememuse--;
			break;
		}
		if (!BN_bin2bn(apppbx->e_crypt_rsa_n, apppbx->e_crypt_rsa_n_len, rsa->n)) {
			derror_bin2bn:
			PERROR("Failed to convert binary to bignum.\n");
			apppbx->e_crypt_keyengine_return = -1;
			RSA_free(rsa);
			ememuse--;
			break;
		}
		if (!BN_bin2bn(apppbx->e_crypt_rsa_e, apppbx->e_crypt_rsa_e_len, rsa->e))
			goto derror_bin2bn;
		if (!BN_bin2bn(apppbx->e_crypt_rsa_d, apppbx->e_crypt_rsa_d_len, rsa->d))
			goto derror_bin2bn;
		if (!BN_bin2bn(apppbx->e_crypt_rsa_p, apppbx->e_crypt_rsa_p_len, rsa->p))
			goto derror_bin2bn;
		if (!BN_bin2bn(apppbx->e_crypt_rsa_q, apppbx->e_crypt_rsa_q_len, rsa->q))
			goto derror_bin2bn;
		if (!BN_bin2bn(apppbx->e_crypt_rsa_dmp1, apppbx->e_crypt_rsa_dmp1_len, rsa->dmp1))
			goto derror_bin2bn;
		if (!BN_bin2bn(apppbx->e_crypt_rsa_dmq1, apppbx->e_crypt_rsa_dmq1_len, rsa->dmq1))
			goto derror_bin2bn;
		if (!BN_bin2bn(apppbx->e_crypt_rsa_iqmp, apppbx->e_crypt_rsa_iqmp_len, rsa->iqmp))
			goto derror_bin2bn;
		PDEBUG(DEBUG_CRYPT, "decrypt: ckey =%02x%02x%02x%02x... (len=%d)\n", apppbx->e_crypt_ckey[0], apppbx->e_crypt_ckey[1], apppbx->e_crypt_ckey[2], apppbx->e_crypt_ckey[3], apppbx->e_crypt_ckey_len);
		apppbx->e_crypt_key_len = RSA_private_decrypt(
			apppbx->e_crypt_ckey_len,
			apppbx->e_crypt_ckey,
			apppbx->e_crypt_key,
			rsa,
			RSA_PKCS1_PADDING);
		PDEBUG(DEBUG_CRYPT, "decrypt: key =%02x%02x%02x%02x... (len=%d)\n", apppbx->e_crypt_key[0], apppbx->e_crypt_key[1], apppbx->e_crypt_key[2], apppbx->e_crypt_key[3], apppbx->e_crypt_key_len);
		RSA_free(rsa);
		ememuse--;
		apppbx->e_crypt_keyengine_return = 1;
#endif
		break;

		default:
		PERROR("Unknown job %d\n", job);
		apppbx->e_crypt_keyengine_return = -1;
	}

	done:
	PDEBUG((DEBUG_EPOINT | DEBUG_CRYPT), "child process done after using libcrypto with return value %d\n", apppbx->e_crypt_keyengine_return);

	/* exit process */
	apppbx->ea_endpoint->ep_use--;
	FREE(args, sizeof(struct auth_args));
	amemuse--;
	return(NULL);
}

void EndpointAppPBX::cryptman_keyengine(int job)
{
	struct auth_args *arg;
	pthread_t tid;

	if (e_crypt_keyengine_busy) {
		e_crypt_keyengine_return = -1;
		PERROR("engine currently busy.\n");
		return;
	}

	arg = (struct auth_args *)MALLOC(sizeof(struct auth_args));
	arg->apppbx = this;
	arg->job = job;
	e_crypt_keyengine_return = 0;
	e_crypt_keyengine_busy = job;

	ea_endpoint->ep_use++;
	if ((pthread_create(&tid, NULL, keyengine_child, arg)<0)) {
		ea_endpoint->ep_use--;
		PERROR("failed to create keyengine-thread.\n");
		e_crypt_keyengine_return = -1;
		return;
	}
	amemuse++;

	PDEBUG((DEBUG_EPOINT | DEBUG_CRYPT), "send_mail(%d): child process created for doing crypto stuff\n", ea_endpoint->ep_serial);

}


/* handler for authentication (called by apppbx's handler)
 */
void EndpointAppPBX::cryptman_handler(void)
{
	if (e_crypt_keyengine_busy) {
		if (e_crypt_keyengine_return < 0) {
			e_crypt_keyengine_busy = 0;
			cryptman_message(CK_ERROR_IND, NULL, 0);
		} else
		if (e_crypt_keyengine_return > 0) {
			switch(e_crypt_keyengine_busy) {
				case CK_GENRSA_REQ:
				e_crypt_keyengine_busy = 0;
				cryptman_message(CK_GENRSA_CONF, NULL, 0);
				break;
				case CK_CPTRSA_REQ:
				e_crypt_keyengine_busy = 0;
				cryptman_message(CK_CPTRSA_CONF, NULL, 0);
				break;
				case CK_DECRSA_REQ:
				e_crypt_keyengine_busy = 0;
				cryptman_message(CK_DECRSA_CONF, NULL, 0);
				break;
			}
		}
	}

	/* check for event, make next event */
	if (e_crypt_timeout_sec) if (e_crypt_timeout_sec<now_tv.tv_sec || (e_crypt_timeout_sec==now_tv.tv_sec && e_crypt_timeout_usec<now_tv.tv_usec)) {
		e_crypt_timeout_sec = 0;
		e_crypt_timeout_usec = 0;
		cryptman_message(CT_TIMEOUT, NULL, 0);
	}
}


/*
 * process message to the crypt manager
 */
/* remote peer sends ident request */
void EndpointAppPBX::cr_ident(int message, unsigned char *param, int len)
{
	unsigned char buf[4], *p;
	unsigned int bogomips = 0, ran;
	int l;

	l = CM_SIZEOFINF(CM_INFO_RANDOM);
	if (l != 4) {
		PDEBUG(DEBUG_CRYPT, "EPOINT(%d) missing (or corrupt) random number, ignoring (len = %d)\n", ea_endpoint->ep_serial, l);
		return;
	}
	p = CM_GETINF(CM_INFO_RANDOM, buf);
	ran = (p[0]<<24) + (p[1]<<16) + (p[2]<<8) + p[3];
	l = CM_SIZEOFINF(CM_INFO_BOGOMIPS);
	if (l != 4) {
		PDEBUG(DEBUG_CRYPT, "EPOINT(%d) missing (or corrupt) random bogomips, just comparing random (len = %d)\n", ea_endpoint->ep_serial, l);
		goto compare_random;
	}
	p = CM_GETINF(CM_INFO_BOGOMIPS, buf);
	bogomips = (p[0]<<24) + (p[1]<<16) + (p[2]<<8) + p[3];
	if (e_crypt_bogomips > bogomips) {
		PDEBUG(DEBUG_CRYPT, "EPOINT(%d) our cpu is faster, so we are master (%d > %d)\n", ea_endpoint->ep_serial, e_crypt_bogomips, bogomips);
		cr_master(message, NULL, 0);
		return;
	}
	if (e_crypt_bogomips < bogomips) {
		PDEBUG(DEBUG_CRYPT, "EPOINT(%d) our cpu is slower, so we are slave (%d < %d)\n", ea_endpoint->ep_serial, e_crypt_bogomips, bogomips);
		cr_slave(message, NULL, 0);
		return;
	}
	PDEBUG(DEBUG_CRYPT, "EPOINT(%d) our cpu is equal speed, so we check for random value (%d == %d)\n", ea_endpoint->ep_serial, e_crypt_bogomips, bogomips);
	compare_random:
	/* bogomips are equal, so we compare */
	if (e_crypt_random > ran) {
		PDEBUG(DEBUG_CRYPT, "EPOINT(%d) our random value is greater, so we are master (%d > %d)\n", ea_endpoint->ep_serial, e_crypt_random, ran);
		cr_master(message, NULL, 0);
		return;
	}
	if (e_crypt_random < ran) {
		PDEBUG(DEBUG_CRYPT, "EPOINT(%d) our random value is smaller, so we are slave (%d < %d)\n", ea_endpoint->ep_serial, e_crypt_random, ran);
		cr_slave(message, NULL, 0);
		return;
	}
	PDEBUG(DEBUG_CRYPT, "EPOINT(%d) random values are equal, so we are looped (%d == %d)\n", ea_endpoint->ep_serial, e_crypt_random, ran);
	cr_looped(message, NULL, 0);
}

/* key-exchange activation by the user */
void EndpointAppPBX::cr_activate(int message, unsigned char *param, int len)
{
	unsigned char buf[128] = "";
	unsigned char msg;
	unsigned char bogomips[4], ran[4];

	/* activate listener */
	cryptman_msg2crengine(CR_LISTEN_REQ, NULL, 0);
	/* send ident message */
	msg = CMSG_IDENT;
	CM_ADDINF(CM_INFO_MESSAGE, 1, &msg);
	/* random number element */
	srandom(now_tv.tv_sec ^ now_tv.tv_usec ^ random());
	e_crypt_random = random();
	ran[0] = e_crypt_random >> 24;
	ran[1] = e_crypt_random >> 16;
	ran[2] = e_crypt_random >> 8;
	ran[3] = e_crypt_random;
	CM_ADDINF(CM_INFO_RANDOM, 4, ran);
	/* cpu speed element */
	e_crypt_bogomips = get_bogomips();
	if (e_crypt_bogomips > 0) {
		bogomips[0] = e_crypt_bogomips >> 24;
		bogomips[1] = e_crypt_bogomips >> 16;
		bogomips[2] = e_crypt_bogomips >> 8;
		bogomips[3] = e_crypt_bogomips;
		CM_ADDINF(CM_INFO_BOGOMIPS, 4, bogomips);
	}
	/* send ident message */
	cryptman_msg2peer(buf);
	/* change state */
	cryptman_state(CM_ST_IDENT);
	/* set timeout */
	cryptman_timeout(CM_TO_IDENT);
}

/* deactivation by the user */
void EndpointAppPBX::cr_deactivate(int message, unsigned char *param, int len)
{
	unsigned char buf[128] = "";
	unsigned char msg;

	/* deactivate listener (if not already) */
	cryptman_msg2crengine(CR_UNLISTEN_REQ, NULL, 0);
	/* message */
	msg = CMSG_ABORT;
	CM_ADDINF(CM_INFO_MESSAGE, 1, &msg);
	cryptman_msg2peer(buf);
	/* deactivate encryption */
	cryptman_msg2crengine(CC_DACT_REQ, NULL, 0);
	/* change state */
	cryptman_state(CM_ST_NULL);
	/* send message to user */
	cryptman_msg2user(CU_DACT_CONF, "Deactivated");
}

/* remote peer tells us to be master */
void EndpointAppPBX::cr_master(int message, unsigned char *param, int len)
{
	unsigned char buf[128] = "";
	unsigned char msg;

	/* change to master state */
	cryptman_state(CM_ST_KEYGEN);
	if (message == CP_IDENT) {
		/* send you-are-slave-message */
		msg = CMSG_SLAVE;
		CM_ADDINF(CM_INFO_MESSAGE, 1, &msg);
		cryptman_msg2peer(buf);
	}
	/* start generation of key */
	cryptman_keyengine(CK_GENRSA_REQ);
	/* disable timeout */
	cryptman_timeout(0);
	/* send message to user */
	cryptman_msg2user(CU_INFO_IND, "Master");
}

/* remote peer tells us to be slave */
void EndpointAppPBX::cr_slave(int message, unsigned char *param, int len)
{
	unsigned char buf[128] = "";
	unsigned char msg;

	/* change to slave state */
	cryptman_state(CM_ST_KEYWAIT);
	if (message == CP_IDENT) {
		/* send you-are-slave-message */
		msg = CMSG_MASTER;
		/* message */
		CM_ADDINF(CM_INFO_MESSAGE, 1, &msg);
		cryptman_msg2peer(buf);
	}
	/* set timeout */
	cryptman_timeout(CM_TO_PUBKEY);
	/* send message to user */
	cryptman_msg2user(CU_INFO_IND, "Slave");
}

/* remote peer tells us about loop */
void EndpointAppPBX::cr_looped(int message, unsigned char *param, int len)
{
	unsigned char buf[128] = "";
	unsigned char msg;

	/* change to idle state */
	cryptman_state(CM_ST_NULL);
	/* deactivate listener */
	cryptman_msg2crengine(CR_UNLISTEN_REQ, NULL, 0);
	if (message == CP_IDENT) {
		/* send looped */
		msg = CMSG_LOOPED;
		/* message */
		CM_ADDINF(CM_INFO_MESSAGE, 1, &msg);
		cryptman_msg2peer(buf);
	}
	/* disable timeout */
	cryptman_timeout(0);
	/* send message to user */
	cryptman_msg2user(CU_ERROR_IND, "Loop Detected");
}

/* abort */
void EndpointAppPBX::cr_abort(int message, unsigned char *param, int len)
{
	/* if already encrypting */
	if (e_crypt_state==CM_ST_WAIT_CRYPT
	 || e_crypt_state==CM_ST_SWAIT
	 || e_crypt_state==CM_ST_ACTIVE) {
		/* deactivate blowfish */
		cryptman_msg2crengine(CC_DACT_REQ, NULL, 0);
	}
	/* change to idle state */
	cryptman_state(CM_ST_NULL);
	/* deactivate listener */
	cryptman_msg2crengine(CR_UNLISTEN_REQ, NULL, 0);
	/* disable timeout */
	cryptman_timeout(0);
	/* send message to user */
	if (message == CT_TIMEOUT)
		cryptman_msg2user(CU_ERROR_IND, "Timeout");
	else if (message == CP_ABORT)
		cryptman_msg2user(CU_ERROR_IND, "Remote Abort");
	else
		cryptman_msg2user(CU_DACT_IND, NULL);
}

/* abort but wait for engine to release*/
void EndpointAppPBX::cr_abort_engine(int message, unsigned char *param, int len)
{
	/* change to release state */
	cryptman_state(CM_ST_RELEASE);
	/* deactivate listener */
	cryptman_msg2crengine(CR_UNLISTEN_REQ, NULL, 0);
	/* disable timeout */
	cryptman_timeout(0);
	/* send message to user */
	if (message == CT_TIMEOUT)
		cryptman_msg2user(CU_ERROR_IND, "Timeout");
	else if (message == CP_ABORT)
		cryptman_msg2user(CU_ERROR_IND, "Remote Abort");
	else
		cryptman_msg2user(CU_DACT_IND, NULL);
}

/* abort and disable crypt engine */
void EndpointAppPBX::cr_abort_wait(int message, unsigned char *param, int len)
{
	/* change to idle state */
	cryptman_state(CM_ST_NULL);
	/* deactivate listener (if not already) */
	cryptman_msg2crengine(CR_UNLISTEN_REQ, NULL, 0);
	/* deactivate blowfish */
	cryptman_msg2crengine(CC_DACT_REQ, NULL, 0);
	/* disable timeout */
	cryptman_timeout(0);
	/* send message to user */
	if (message == CT_TIMEOUT)
		cryptman_msg2user(CU_ERROR_IND, "Timeout");
	else if (message == CP_ABORT)
		cryptman_msg2user(CU_ERROR_IND, "Remote Abort");
	else
		cryptman_msg2user(CU_DACT_IND, NULL);
}

/* key engine tells us that the rsa is ready */
void EndpointAppPBX::cr_genrsa(int message, unsigned char *param, int len)
{
	unsigned char buf[1024] = "";
	unsigned char msg;

	/* change to wait for crypted session key state */
	cryptman_state(CM_ST_CSWAIT);
	/* message */
	msg = CMSG_PUBKEY;
	CM_ADDINF(CM_INFO_MESSAGE, 1, &msg);
	CM_ADDINF(CM_INFO_PUBKEY, e_crypt_rsa_n_len, &e_crypt_rsa_n);
	CM_ADDINF(CM_INFO_PUBEXPONENT, e_crypt_rsa_e_len, &e_crypt_rsa_e);
	cryptman_msg2peer(buf);
	/* set timeout */
	cryptman_timeout(CM_TO_CSKEY);
}

/* our engine has a key error */
void EndpointAppPBX::cr_keyerror(int message, unsigned char *param, int len)
{
	unsigned char buf[128] = "";
	unsigned char msg;

	/* change to idle state */
	cryptman_state(CM_ST_NULL);
	/* deactivate listener */
	cryptman_msg2crengine(CR_UNLISTEN_REQ, NULL, 0);
	/* message */
	msg = CMSG_ABORT;
	CM_ADDINF(CM_INFO_MESSAGE, 1, &msg);
	cryptman_msg2peer(buf);
	/* send message to user */
	cryptman_msg2user(CU_ERROR_IND, "Local Key Error");
}

/* remote sends us the rsa public key */
void EndpointAppPBX::cr_pubkey(int message, unsigned char *param, int len)
{
	unsigned char buf[128] = "";
	unsigned char msg = CMSG_ABORT;
	int l;

	l = CM_SIZEOFINF(CM_INFO_PUBKEY);
	if (l<1 || l>(int)sizeof(e_crypt_rsa_n)) {
		size_error:
		/* change to idle state */
		cryptman_state(CM_ST_NULL);
		/* deactivate listener */
		cryptman_msg2crengine(CR_UNLISTEN_REQ, NULL, 0);
		/* message */
		CM_ADDINF(CM_INFO_MESSAGE, 1, &msg);
		cryptman_msg2peer(buf);
		/* send message to user */
		cryptman_msg2user(CU_ERROR_IND, "Remote Key Error");
		return;
	}
	CM_GETINF(CM_INFO_PUBKEY, e_crypt_rsa_n);
	e_crypt_rsa_n_len = l;
	l = CM_SIZEOFINF(CM_INFO_PUBEXPONENT);
	if (l<1 || l>(int)sizeof(e_crypt_rsa_e))
		goto size_error;
	CM_GETINF(CM_INFO_PUBEXPONENT, e_crypt_rsa_e);
	e_crypt_rsa_e_len = l;
	/* change to generating encrypted sessnion key state */
	cryptman_state(CM_ST_CSKEY);
	/* start generation of crypted session key */
	cryptman_keyengine(CK_CPTRSA_REQ);
	/* disable timeout */
	cryptman_timeout(0);
}

/* key engine tells us that the crypted session key is ready */
void EndpointAppPBX::cr_cptrsa(int message, unsigned char *param, int len)
{
	unsigned char buf[1024] = "";
	unsigned char msg = CMSG_CSKEY;

	/* change to wait for crypt engine state */
	cryptman_state(CM_ST_WAIT_DELAY);
	/* message */
	CM_ADDINF(CM_INFO_MESSAGE, 1, &msg);
	CM_ADDINF(CM_INFO_CSKEY, e_crypt_ckey_len, &e_crypt_ckey);
	cryptman_msg2peer(buf);
	/* deactivate listener */
	cryptman_msg2crengine(CR_UNLISTEN_REQ, NULL, 0);
	/* timeout 1 sec */
	cryptman_timeout(1);
}

/* now we waited for the remote to receive and decrypt the session key */
void EndpointAppPBX::cr_waitdelay(int message, unsigned char *param, int len)
{
	/* change to wait for crypt engine state */
	cryptman_state(CM_ST_WAIT_CRYPT);
	/* disable timeout */
	cryptman_timeout(0);
	/* send message to crypt engine */
	cryptman_msg2crengine(CC_ACTBF_REQ, e_crypt_key, e_crypt_key_len);
}

/* remote sends us the crypted session key */
void EndpointAppPBX::cr_cskey(int message, unsigned char *param, int len)
{
	unsigned char buf[128] = "";
	unsigned char msg = CMSG_ABORT;
	int l;

	/* disable timeout */
	cryptman_timeout(0);
	l = CM_SIZEOFINF(CM_INFO_CSKEY);
	if (l<1 || l>(int)sizeof(e_crypt_ckey)) {
		/* change to idle state */
		cryptman_state(CM_ST_NULL);
		/* deactivate listener */
		cryptman_msg2crengine(CR_UNLISTEN_REQ, NULL, 0);
		/* message */
		CM_ADDINF(CM_INFO_MESSAGE, 1, &msg);
		cryptman_msg2peer(buf);
		/* send message to user */
		cryptman_msg2user(CU_ERROR_IND, "Remote Key Error");
		return;
	}
	CM_GETINF(CM_INFO_CSKEY, e_crypt_ckey);
	e_crypt_ckey_len = l;
	/* change to generating decrypted session key state */
	cryptman_state(CM_ST_SESSION);
	/* start generation of decrypted session key */
	cryptman_keyengine(CK_DECRSA_REQ);
}

/* key engine tells us that the decrypted session key is ready */
void EndpointAppPBX::cr_decrsa(int message, unsigned char *param, int len)
{
	/* change to wait for crypt engine state */
	cryptman_state(CM_ST_WAIT_CRYPT);
	/* deactivate listener */
	cryptman_msg2crengine(CR_UNLISTEN_REQ, NULL, 0);
	/* send message to crypt engine */
	cryptman_msg2crengine(CC_ACTBF_REQ, e_crypt_key, e_crypt_key_len);
}

/* blowfish now active */
void EndpointAppPBX::cr_bfactive(int message, unsigned char *param, int len)
{
	char text[64];

	/* change to active state */
	cryptman_state(CM_ST_ACTIVE);
	/* send message to user */
	SPRINT(text, "PUB %02x%02x %02x%02x %02x%02x %02x%02x", e_crypt_key[0], e_crypt_key[1], e_crypt_key[2], e_crypt_key[3], e_crypt_key[4], e_crypt_key[5], e_crypt_key[6], e_crypt_key[7]);
	cryptman_msg2user(CU_ACTK_CONF, text);
}

/* our crypt engine sends an error */
void EndpointAppPBX::cr_crypterror(int message, unsigned char *param, int len)
{
	unsigned char buf[128] = "";
	unsigned char msg = CMSG_ABORT;

	/* change to idle state */
	cryptman_state(CM_ST_NULL);
	/* deactivate listener */
	cryptman_msg2crengine(CR_UNLISTEN_REQ, NULL, 0);
	/* message */
	CM_ADDINF(CM_INFO_MESSAGE, 1, &msg);
	cryptman_msg2peer(buf);
	/* send message to user */
	cryptman_msg2user(CU_ERROR_IND, "Blowfish Error");
}

/* engine is done, now we are done with release */
void EndpointAppPBX::cr_release(int message, unsigned char *param, int len)
{
	/* change to idle state */
	cryptman_state(CM_ST_NULL);
}

/* activate using shared key */
void EndpointAppPBX::cr_sactivate(int message, unsigned char *param, int len)
{
	/* change to 'wait for crypt engine' state */
	cryptman_state(CM_ST_SWAIT);
	/* disable timeout */
	cryptman_timeout(0);
	/* send key to crypt engine */
	cryptman_msg2crengine(CC_ACTBF_REQ, param, len);
}

/* share key deactivation by the user */
void EndpointAppPBX::cr_sdeactivate(int message, unsigned char *param, int len)
{
	/* deactivate encryption */
	cryptman_msg2crengine(CC_DACT_REQ, NULL, 0);
	/* change state */
	cryptman_state(CM_ST_NULL);
	/* send message to user */
	cryptman_msg2user(CU_DACT_CONF, NULL);
}

/* shared key abort */
void EndpointAppPBX::cr_sabort(int message, unsigned char *param, int len)
{
	/* change to idle state */
	cryptman_state(CM_ST_NULL);
	/* send message to user */
	cryptman_msg2user(CU_DACT_IND, "Deactivated");
}

/* shared key: our crypt engine sends an error */
void EndpointAppPBX::cr_scrypterror(int message, unsigned char *param, int len)
{
	/* change to idle state */
	cryptman_state(CM_ST_NULL);
	/* send message to user */
	cryptman_msg2user(CU_ERROR_IND, "Blowfish Error");
}

/* blowfish now active */
void EndpointAppPBX::cr_sbfactive(int message, unsigned char *param, int len)
{
	char text[64];

	/* change to active state */
	cryptman_state(CM_ST_SACTIVE);
	/* send message to user */
	SPRINT(text, "Call Secure");
	cryptman_msg2user(CU_ACTS_CONF, text);
}

/* user requests info */
void EndpointAppPBX::cr_info(int message, unsigned char *param, int len)
{
	/* send message to user */
	cryptman_msg2user(CU_INFO_CONF, e_crypt_info);
}


CM_MSG_NAMES

void EndpointAppPBX::cryptman_message(int message, unsigned char *param, int len)
{
	const char *msgtext = "<<UNKNOWN MESSAGE>>";

	if (message>=0 && message<cm_msg_num)
		msgtext = cm_msg_name[message];

	PDEBUG(DEBUG_CRYPT, "EPOINT(%d) CRYPT MANAGER in state '%s' received message: %s len: %d\n", ea_endpoint->ep_serial, statename(e_crypt_state), msgtext, len);

	/* all states */
	if (message == CU_INFO_REQ)
		{ cr_info(message, param, len); return; }

	switch(e_crypt_state) {
		/* in idle state */
		case CM_ST_NULL:
		if (message == CU_ACTK_REQ) /* request key-exchange encryption */
			{ cr_activate(message, param, len); return; }
		if (message == CU_ACTS_REQ) /* request shared encryption */
			{ cr_sactivate(message, param, len); return; }
		break;

		/* identifying state */
		case CM_ST_IDENT:
		if (message == CP_IDENT) /* request encryption */
			{ cr_ident(message, param, len); return; }
		if (message == CP_SLAVE) /* we are slave */
			{ cr_slave(message, param, len); return; }
		if (message == CP_MASTER) /* we are master */
			{ cr_master(message, param, len); return; }
		if (message == CP_LOOPED) /* we are looped */
			{ cr_looped(message, param, len); return; }
		if (message == CI_DISCONNECT_IND /* request aborting */
		 || message == CT_TIMEOUT /* timeout */
		 || message == CP_ABORT) /* request aborting */
			{ cr_abort(message, param, len); return; }
		break;

		/* generating public key state */
		case CM_ST_KEYGEN:
		if (message == CK_GENRSA_CONF) /* public key is done */
			{ cr_genrsa(message, param, len); return; }
		if (message == CK_ERROR_IND) /* key failed */
			{ cr_keyerror(message, param, len); return; }
		if (message == CI_DISCONNECT_IND /* request aborting */
		 || message == CP_ABORT) /* request aborting */
			{ cr_abort_engine(message, param, len); return; }
		break;

		/* waiting for public key state */
		case CM_ST_KEYWAIT:
		if (message == CP_PUBKEY) /* getting public key from remote */
			{ cr_pubkey(message, param, len); return; }
		if (message == CI_DISCONNECT_IND /* request aborting */
		 || message == CT_TIMEOUT /* timeout */
		 || message == CP_ABORT) /* request aborting */
			{ cr_abort(message, param, len); return; }
		break;

		/* generating crypted session key state */
		case CM_ST_CSKEY:
		if (message == CK_CPTRSA_CONF) /* crypted session key is done */
			{ cr_cptrsa(message, param, len); return; }
		if (message == CK_ERROR_IND) /* key failed */
			{ cr_keyerror(message, param, len); return; }
		if (message == CI_DISCONNECT_IND /* request aborting */
		 || message == CP_ABORT) /* request aborting */
			{ cr_abort_engine(message, param, len); return; }
		break;

		/* waiting for crypted session key state */
		case CM_ST_CSWAIT:
		if (message == CP_CSKEY) /* getting crypted session key from remote */
			{ cr_cskey(message, param, len); return; }
		if (message == CI_DISCONNECT_IND /* request aborting */
		 || message == CT_TIMEOUT /* timeout */
		 || message == CP_ABORT) /* request aborting */
			{ cr_abort(message, param, len); return; }
		break;

		/* generating decrypted session key state */
		case CM_ST_SESSION:
		if (message == CK_DECRSA_CONF) /* decrypted is done */
			{ cr_decrsa(message, param, len); return; }
		if (message == CK_ERROR_IND) /* key failed */
			{ cr_keyerror(message, param, len); return; }
		if (message == CI_DISCONNECT_IND /* request aborting */
		 || message == CP_ABORT) /* request aborting */
			{ cr_abort_engine(message, param, len); return; }
		break;

		/* wait encryption on state */
		case CM_ST_WAIT_DELAY:
		if (message == CT_TIMEOUT) /* timeout of delay */
			{ cr_waitdelay(message, param, len); return; }
		if (message == CC_ERROR_IND) /* encrpytion error */
			{ cr_crypterror(message, param, len); return; }
		if (message == CI_DISCONNECT_IND /* request aborting */
		 || message == CP_ABORT) /* request aborting */
			{ cr_abort_wait(message, param, len); return; }
		break;

		/* wait encryption on state */
		case CM_ST_WAIT_CRYPT:
		if (message == CC_ACTBF_CONF) /* encrpytion active */
			{ cr_bfactive(message, param, len); return; }
		if (message == CC_ERROR_IND) /* encrpytion error */
			{ cr_crypterror(message, param, len); return; }
		if (message == CI_DISCONNECT_IND /* request aborting */
		 || message == CP_ABORT) /* request aborting */
			{ cr_abort_wait(message, param, len); return; }
		break;

		/* active state */
		case CM_ST_ACTIVE:
		if (message == CU_DACT_REQ) /* deactivating encryption */
			{ cr_deactivate(message, param, len); return; }
		if (message == CI_DISCONNECT_IND /* request aborting */
		 || message == CP_ABORT) /* request aborting */
			{ cr_abort(message, param, len); return; }
		break;


		/* engine done after abort state */
		case CM_ST_RELEASE:
		if (message == CK_GENRSA_CONF /* engine done */
		 || message == CK_CPTRSA_CONF /* engine done */
		 || message == CK_DECRSA_CONF /* engine done */
		 || message == CK_ERROR_IND) /* engine error */
			{ cr_release(message, param, len); return; }
		break;

		/* shared active state */
		case CM_ST_SACTIVE:
		if (message == CU_DACT_REQ) /* deactivating encryption */
			{ cr_sdeactivate(message, param, len); return; }
		if (message == CI_DISCONNECT_IND) /* request aborting */
			{ cr_sabort(message, param, len); return; }
		break;

		/* wait shared encryption on state */
		case CM_ST_SWAIT:
		if (message == CC_ACTBF_CONF) /* encrpytion active */
			{ cr_sbfactive(message, param, len); return; }
		if (message == CC_ERROR_IND) /* encrpytion error */
			{ cr_scrypterror(message, param, len); return; }
		if (message == CI_DISCONNECT_IND) /* request aborting */
			{ cr_sabort(message, param, len); return; }
		break;

	}

	PDEBUG(DEBUG_CRYPT, "message not handled in state %d\n", e_crypt_state);
}


/*
 * analyze the message element within the received message from peer and call 'cryptman_message'
 */
void EndpointAppPBX::cryptman_msg2man(unsigned char *param, int len)
{
	unsigned char *p;
	unsigned char msg;
	int i, l;

	/* check if frame is correct */
	PDEBUG(DEBUG_CRYPT, "EPOINT(%d) message from peer to crypt_manager.\n", ea_endpoint->ep_serial);
	if (len == 0) {
		PDEBUG(DEBUG_CRYPT, "ignoring message with 0-length.\n");
		return;
	}
	i = 0;
	p = param;
	while(*p) {
		if (i == len) {
			PDEBUG(DEBUG_CRYPT, "end of message without 0-termination.\n");
			return;
		}
		if (i+3 > len) {
			PDEBUG(DEBUG_CRYPT, "message with element size, outside the frame length.\n");
			return;
		}
		l = (p[1]<<8) + p[2];
//		PDEBUG(DEBUG_CRYPT, "   inf %d (len = %d)\n", *p, l);
		if (i+3+l > len) {
			PDEBUG(DEBUG_CRYPT, "message with element data, outside the frame length.\n");
			return;
		}
		i += l + 3;
		p += l + 3;
	}
	if (i+1 != len) {
		PDEBUG(DEBUG_CRYPT, "warning: received null-element before end of frame.\n");
	}

	l = CM_SIZEOFINF(CM_INFO_MESSAGE);
	if (l != 1) {
		PDEBUG(DEBUG_CRYPT, "received message without (valid) message element (len = %d)\n", len);
		return;
	}
	CM_GETINF(CM_INFO_MESSAGE, &msg);
	switch (msg) {
		case CMSG_IDENT:
		cryptman_message(CP_IDENT, param, len);
		break;
		case CMSG_SLAVE:
		cryptman_message(CP_SLAVE, param, len);
		break;
		case CMSG_MASTER:
		cryptman_message(CP_MASTER, param, len);
		break;
		case CMSG_PUBKEY:
		cryptman_message(CP_PUBKEY, param, len);
		break;
		case CMSG_CSKEY:
		cryptman_message(CP_CSKEY, param, len);
		break;
		case CMSG_ABORT:
		cryptman_message(CP_ABORT, param, len);
		break;
		default:
		PDEBUG(DEBUG_CRYPT, "received unknown message element %d\n", msg);
	}
}

/* add information element to buffer
 */
void EndpointAppPBX::cryptman_addinf(unsigned char *buf, int buf_size, int element, int len, void *data)
{
	int l;

	/* skip what we already have in the buffer */
	while (buf[0]) {
		l = (buf[1]<<8) + buf[2];
		if (l >= buf_size-3) {
			PERROR("EPOINT(%d) buffer overflow while adding information to peer message.\n", ea_endpoint->ep_serial);
			return;
		}
		buf_size -= l + 3;
		buf += l + 3;
	}
	/* check if we have not enough space to add element including id, len, data, and the null-termination */
	if (len+4 > buf_size) {
		PERROR("EPOINT(%d) cannot add element to message, because buffer would overflow.\n", ea_endpoint->ep_serial);
		return;
	}
	buf[0] = element;
	buf[1] = len >> 8;
	buf[2] = len;
	memcpy(buf+3, data, len);
}


/* get size of element in buffer
 */
int EndpointAppPBX::cryptman_sizeofinf(unsigned char *buf, int element)
{
	int l;

	/* skip what we already have in the buffer */
	while (buf[0]) {
		l = (buf[1]<<8) + buf[2];
		if (buf[0] == element)
			return(l);
		buf += l + 3;
	}
	return(-1);
}


/* get information element from buffer
 */
unsigned char *EndpointAppPBX::cryptman_getinf(unsigned char *buf, int element, unsigned char *to)
{
	int l;

	/* skip what we already have in the buffer */
	while (buf[0]) {
		l = (buf[1]<<8) + buf[2];
		if (buf[0] == element) {
			memcpy(to, buf+3, l);
			return(to);
		}
		buf += l + 3;
	}
	return(NULL);
}


/* send message to peer
 */
void EndpointAppPBX::cryptman_msg2peer(unsigned char *buf)
{
	struct lcr_msg *message;
	unsigned char *p = buf;
	int len = 0;
	int l;

	/* get len */
	while(p[0]) {
		l = (p[1]<<8) + p[2];
		len += l + 3;
		p += l + 3;
	}
	if (len+1 > (int)sizeof(message->param.crypt.data)) {
		PERROR("EPOINT(%d) message larger than allowed in param->crypt.data.\n", ea_endpoint->ep_serial);
		return;
	}
	/* send message */
	message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_portlist->port_id, EPOINT_TO_PORT, MESSAGE_CRYPT);
	message->param.crypt.type = CR_MESSAGE_REQ;
	message->param.crypt.len = len+1;
	memcpy(message->param.crypt.data, buf, len+1);
	message_put(message);

	if (options.deb & DEBUG_CRYPT) {
		PDEBUG(DEBUG_CRYPT, "EPOINT(%d) sending message\n", ea_endpoint->ep_serial);
		p = buf;
		while(p[0]) {
			l = (p[1]<<8) + p[2];
			PDEBUG(DEBUG_CRYPT, "   inf %d (len = %d)\n", p[0], l);
			len += l + 3;
			p += l + 3;
		}
	}
}

/* send message to crypt engine
 */
void EndpointAppPBX::cryptman_msg2crengine(int msg, unsigned char *buf, int len)
{
	struct lcr_msg *message;

	if (len > (int)sizeof(message->param.crypt.data)) {
		PERROR("EPOINT(%d) message larger than allowed in param->crypt.data.\n", ea_endpoint->ep_serial);
		return;
	}
	/* send message */
	message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_portlist->port_id, EPOINT_TO_PORT, MESSAGE_CRYPT);
	message->param.crypt.type = msg;
	message->param.crypt.len = len;
	if (len)
		memcpy(message->param.crypt.data, buf, len);
	message_put(message);

	if (options.deb & DEBUG_CRYPT) {
		const char *msgtext = "<<UNKNOWN MESSAGE>>";

		if (msg>=0 && msg<cm_msg_num)
			msgtext = cm_msg_name[msg];
		PDEBUG(DEBUG_CRYPT, "EPOINT(%d) sending message '%s' (len = %d)\n", ea_endpoint->ep_serial, msgtext, len);
	}
}

/* send message to user
 */
void EndpointAppPBX::cryptman_msg2user(int msg, const char *text)
{
	struct lcr_msg *message;
	/* send message */
	message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_join_id, EPOINT_TO_JOIN, MESSAGE_CRYPT);
	message->param.crypt.type = msg;
	if (!text)
		text = "";
	SCPY(e_crypt_info, text);
	if (text[0]) {
		UNCPY((char *)message->param.crypt.data, e_crypt_info, sizeof(message->param.crypt.data)-1);
		message->param.crypt.len = strlen((char *)message->param.crypt.data)+1;
	}
	message_put(message);

	if (options.deb & DEBUG_CRYPT) {
		const char *msgtext = "<<UNKNOWN MESSAGE>>";

		if (msg>=0 && msg<cm_msg_num)
			msgtext = cm_msg_name[msg];
		PDEBUG(DEBUG_CRYPT, "EPOINT(%d) sending message '%s' (text = %s)\n", ea_endpoint->ep_serial, msgtext, text?text:"");
	}
}

/* change state
 */
void EndpointAppPBX::cryptman_state(int state)
{
	PDEBUG(DEBUG_CRYPT, "Changing state from %s to %s\n", statename(e_crypt_state), statename(state));
	e_crypt_state = state;
}


/* set timeout
 */
void EndpointAppPBX::cryptman_timeout(int secs)
{
	if (secs) {
		e_crypt_timeout_sec = now_tv.tv_sec+secs;
		e_crypt_timeout_usec = now_tv.tv_usec;
		PDEBUG(DEBUG_CRYPT, "Changing timeout to %d seconds\n", secs);
	} else {
		e_crypt_timeout_sec = 0;
		e_crypt_timeout_usec = 0;
		PDEBUG(DEBUG_CRYPT, "turning timeout off\n", secs);
	}
}

/* encode a message to be sent via b-channel
 */
int cryptman_encode_bch(unsigned char *data, int len, unsigned char *buf, int buf_len)
{
	unsigned int crc;
	int overhead = 18;

	len--; /* without null-termination */
	if (buf_len < len+overhead) {
		PERROR("frame too long for buffer");
		return(0);
	}
	PDEBUG(DEBUG_CRYPT, "encoding a block of %d bytes.\n", len);

	/* write identification sequence to the header */
	UNCPY((char *)buf, "CRYPTMAN" ,8);
	buf += 8;
	/* length + checksumme */
	buf[0] = len >> 8;
	buf[1] = len & 0xff;
	crc = crc32(buf, 2);
	buf += 2;
	buf[0] = crc >> 24;
	buf[1] = crc >> 16;
	buf[2] = crc >> 8;
	buf[3] = crc;
	buf += 4;
	/* data + checksumme */
	memcpy(buf, data, len);
	crc = crc32(buf, len);
	buf += len;
	buf[0] = crc >> 24;
	buf[1] = crc >> 16;
	buf[2] = crc >> 8;
	buf[3] = crc;
	buf += 4;
	return(len + overhead);
}
	
/* decode a message from b-channel
 */
void PmISDN::cryptman_listen_bch(unsigned char *p, int l)
{
	int i;
	struct lcr_msg *message;

	retry:
	if (!l)
		return;

	/* check for the keyword */
	if (p_m_crypt_listen_state == 0) {
		while((*p++)!='C' && l)
			l--;
		if (!l)
			return;
		l--;
		p_m_crypt_listen_state++;
		if (!l)
			return;
	}
	if (p_m_crypt_listen_state < 8) {
		i = p_m_crypt_listen_state;
		while(i < 8) {
			l--;
			if (*p++ != "CRYPTMAN"[i]) {
				p_m_crypt_listen_state = 0;
				goto retry;
			}
			p_m_crypt_listen_state++;
			if (!l)
				break;
			i++;
		}
		if (!l)
			return;
	}
	/* high byte of length */
	if (p_m_crypt_listen_state == 8) {
		p_m_crypt_listen_len = (*p++) << 8;
		p_m_crypt_listen_state++;
		if (!(--l))
			return;
	}
	/* low byte of length */
	if (p_m_crypt_listen_state == 9) {
		p_m_crypt_listen_len += *p++;
		p_m_crypt_listen_state++;
		if (!(--l))
			return;
	}
	/* crc */
	if (p_m_crypt_listen_state == 10) {
		p_m_crypt_listen_crc = (*p++) << 24;
		p_m_crypt_listen_state++;
		if (!(--l))
			return;
	}
	if (p_m_crypt_listen_state == 11) {
		p_m_crypt_listen_crc += (*p++) << 16;
		p_m_crypt_listen_state++;
		if (!(--l))
			return;
	}
	if (p_m_crypt_listen_state == 12) {
		p_m_crypt_listen_crc += (*p++) << 8;
		p_m_crypt_listen_state++;
		if (!(--l))
			return;
	}
	if (p_m_crypt_listen_state == 13) {
		unsigned char lencheck[2];
		p_m_crypt_listen_crc += *p++;
		/* check for CRC */
		lencheck[0] = p_m_crypt_listen_len >> 8;
		lencheck[1] = p_m_crypt_listen_len & 0xff;
		if (crc32(lencheck, 2) != p_m_crypt_listen_crc) {
			PDEBUG(DEBUG_CRYPT, "PmISDN(%s) received a block of %d bytes, but checksumme of length is incorrect (must %08x is %08x\n", p_name, p_m_crypt_listen_len, crc32(lencheck, 2), p_m_crypt_listen_crc);
			p_m_crypt_listen_state = 0;
			goto retry;
		}
		if (p_m_crypt_listen_len > (int)sizeof(p_m_crypt_listen_msg)) {
			PDEBUG(DEBUG_CRYPT, "PmISDN(%s) received a block of %d bytes, but too big for buffer (%d bytes)\n", p_name, p_m_crypt_listen_len, sizeof(p_m_crypt_listen_msg));
			p_m_crypt_listen_state = 0;
			goto retry;
		}
		if (!p_m_crypt_listen_len) {
			PDEBUG(DEBUG_CRYPT, "PmISDN(%s) received a block of 0 bytes\n", p_name);
			p_m_crypt_listen_state = 0;
			goto retry;
		}
		p_m_crypt_listen_state++;
		if (!(--l))
			return;
	}
	/* read message */
	while (p_m_crypt_listen_state>=14 && p_m_crypt_listen_state<(p_m_crypt_listen_len+14)) {
		p_m_crypt_listen_msg[p_m_crypt_listen_state-14] = *p++;
		p_m_crypt_listen_state++;
		if (!(--l))
			return;
	}
	/* crc */
	if (p_m_crypt_listen_state == 14+p_m_crypt_listen_len) {
		p_m_crypt_listen_crc = (*p++) << 24;
		p_m_crypt_listen_state++;
		if (!(--l))
			return;
	}
	if (p_m_crypt_listen_state == 15+p_m_crypt_listen_len) {
		p_m_crypt_listen_crc += (*p++) << 16;
		p_m_crypt_listen_state++;
		if (!(--l))
			return;
	}
	if (p_m_crypt_listen_state == 16+p_m_crypt_listen_len) {
		p_m_crypt_listen_crc += (*p++) << 8;
		p_m_crypt_listen_state++;
		if (!(--l))
			return;
	}
	if (p_m_crypt_listen_state == 17+p_m_crypt_listen_len) {
		l--;
		p_m_crypt_listen_crc += *p++;
		/* check for CRC */
		if (crc32(p_m_crypt_listen_msg, p_m_crypt_listen_len) != p_m_crypt_listen_crc) {
			PDEBUG(DEBUG_CRYPT, "PmISDN(%s) received a block of %d bytes, but checksumme of data block is incorrect\n", p_name, p_m_crypt_listen_len);
			p_m_crypt_listen_state = 0;
			if (!l)
				return;
			goto retry;
		}
		/* now send message */
		p_m_crypt_listen_state = 0;
		PDEBUG(DEBUG_CRYPT, "PmISDN(%s) received a block of %d bytes sending to crypt manager\n", p_name, p_m_crypt_listen_len);
		if ((int)sizeof(message->param.crypt.data) < p_m_crypt_listen_len+1) /* null-terminated */ {
			PDEBUG(DEBUG_CRYPT, "PmISDN(%s) received a block of %d bytes that is too large for message buffer\n", p_name, p_m_crypt_listen_len);
			if (!l)
				return;
			goto retry;
		}
		message = message_create(p_serial, ACTIVE_EPOINT(p_epointlist), PORT_TO_EPOINT, MESSAGE_CRYPT);
		message->param.crypt.type = CR_MESSAGE_IND;
		message->param.crypt.len = p_m_crypt_listen_len+1; /* null termination */
		memcpy(message->param.crypt.data, p_m_crypt_listen_msg, p_m_crypt_listen_len);
		message_put(message);
		p_m_crypt_listen_state = 0;
		if (!l)
			return;
		goto retry;
	}
}


/* encrypt call using share secret (keypad function)
 */
void EndpointAppPBX::encrypt_shared(void)
{
	struct lcr_msg *message;
	const char *errstr = "";
	class Port *port;
	int type, key_len;
	unsigned char *key;
	char *auth_pointer, *crypt_pointer, *key_pointer;
	int ret;

	/* redisplay current crypt display */
	if (e_crypt != CRYPT_OFF) {
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) encryption in progress, so we request the current message.\n", ea_endpoint->ep_serial);
		message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_join_id, EPOINT_TO_JOIN, MESSAGE_CRYPT);
		message->param.crypt.type = CU_INFO_REQ;
		message_put(message);
		return;
	}

	if (check_external(&errstr, &port)) {
		reject:
		message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_portlist->port_id, EPOINT_TO_PORT, MESSAGE_NOTIFY);
		SCPY(message->param.notifyinfo.display, errstr);
		message_put(message);
		set_tone(ea_endpoint->ep_portlist, "crypt_off");
		e_tone[0] = '\0';
		return;
	}

	/* check the key for the call */
	if (port->p_type==PORT_TYPE_DSS1_TE_OUT || port->p_type==PORT_TYPE_DSS1_NT_OUT)
		ret = parse_secrets((char *)e_ext.number, (char *)port->p_dialinginfo.id, &auth_pointer, &crypt_pointer, &key_pointer);
	else {
		if (!port->p_callerinfo.id[0]) {
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) incoming remote call has no caller ID.\n", ea_endpoint->ep_serial);
			errstr = "No Remote ID";
			goto reject;
		}
		ret = parse_secrets((char *)e_ext.number, (char *)numberrize_callerinfo(port->p_callerinfo.id, port->p_callerinfo.ntype, options.national, options.international), &auth_pointer, &crypt_pointer, &key_pointer);
	}
	if (!ret) {
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) Key was not found.\n", ea_endpoint->ep_serial);
		errstr = "No Key";
		goto reject;
	}
	key = crypt_key((unsigned char *)key_pointer, &key_len);
	if (!key) {
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) Key invalid.\n", ea_endpoint->ep_serial);
		errstr = "Invalid Key";
		goto reject;
	}
	if (key_len > 128) {
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) Key too long.\n", ea_endpoint->ep_serial);
		errstr = "Key Too Long";
		goto reject;
	}
	if (!!strcasecmp(auth_pointer, "manual")) {
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) Wrong authentication method.\n", ea_endpoint->ep_serial);
		errstr = "Wrong Auth Type";
		goto reject;
	}
	if (!strcasecmp(crypt_pointer, "blowfish")) {
		type = CC_ACTBF_REQ;
		if (key_len < 4) {
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) Key too short.\n", ea_endpoint->ep_serial);
			errstr = "Key Too Short";
			goto reject;
		}
		if (key_len > 56) {
			PDEBUG(DEBUG_EPOINT, "EPOINT(%d) Key too long.\n", ea_endpoint->ep_serial);
			errstr = "Key Too Long";
			goto reject;
		}
	} else {
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) Wrong crypt method.\n", ea_endpoint->ep_serial);
		errstr = "Wrong Crypt Type";
		goto reject;
	}

	PDEBUG(DEBUG_EPOINT, "EPOINT(%d) Key is ok, sending activation+key to cryptman.\n", ea_endpoint->ep_serial);
	/* setting display message and state */
//	SPRINT(e_crypt_display, "Shared Key");
	e_crypt = CRYPT_SWAIT;
	/* sending activation */
	message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_join_id, EPOINT_TO_JOIN, MESSAGE_CRYPT);
	message->param.crypt.type = CU_ACTS_REQ;
	message->param.crypt.len = key_len;
	memcpy(message->param.crypt.data, key, key_len);
	message_put(message);
}


/* encrypt call using rsa authentication (keypad function)
 */
void EndpointAppPBX::encrypt_keyex(void)
{
	struct lcr_msg *message;
	const char *errstr = "";
	class Port *port;

	/* redisplay current crypt display */
	if (e_crypt != CRYPT_OFF) {
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) encryption in progress, so we request the current message.\n", ea_endpoint->ep_serial);
		message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_join_id, EPOINT_TO_JOIN, MESSAGE_CRYPT);
		message->param.crypt.type = CU_INFO_REQ;
		message_put(message);
		return;
	}


	if (check_external(&errstr, &port)) {
		message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_portlist->port_id, EPOINT_TO_PORT, MESSAGE_NOTIFY);
		SCPY(message->param.notifyinfo.display, errstr);
		message_put(message);
		set_tone(ea_endpoint->ep_portlist, "crypt_off");
		e_tone[0] = '\0';
		return;
	}

#ifndef CRYPTO
	message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_portlist->port_id, EPOINT_TO_PORT, MESSAGE_NOTIFY);
	SCPY(message->param.notifyinfo.display, "Not Compiled");
	message_put(message);
	set_tone(ea_endpoint->ep_portlist, "crypt_off");
	e_tone[0] = '\0';
#else
	PDEBUG(DEBUG_EPOINT, "EPOINT(%d) Sending key-exchange activation to cryptman.\n", ea_endpoint->ep_serial);
	/* setting display message and state */
//	SPRINT(e_crypt_display, "Key-Exchange");
	message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_portlist->port_id, EPOINT_TO_PORT, MESSAGE_NOTIFY);
	SCPY(message->param.notifyinfo.display, "Key-Exchange");
	message_put(message);
	e_crypt = CRYPT_KWAIT;
	/* sending activation */
	message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_join_id, EPOINT_TO_JOIN, MESSAGE_CRYPT);
	message->param.crypt.type = CU_ACTK_REQ;
	message_put(message);
#endif /* CRYPTO */
}


/* turn encryption off (keypad function)
 */
void EndpointAppPBX::encrypt_off(void)
{
	struct lcr_msg *message;

	if (e_crypt!=CRYPT_ON && e_crypt!=CRYPT_OFF) {
		message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_portlist->port_id, EPOINT_TO_PORT, MESSAGE_NOTIFY);
		SCPY(message->param.notifyinfo.display, "Please Wait");
		message_put(message);
		return;
	}
	if (e_crypt == CRYPT_OFF) {
		message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_portlist->port_id, EPOINT_TO_PORT, MESSAGE_NOTIFY);
		SCPY(message->param.notifyinfo.display, "No Encryption");
		message_put(message);
		set_tone(ea_endpoint->ep_portlist, "crypt_off");
		e_tone[0] = '\0';
		return;
	}

	PDEBUG(DEBUG_EPOINT, "EPOINT(%d) Sending deactivation to cryptman.\n", ea_endpoint->ep_serial);
	/* setting display message and state */
//	SPRINT(e_crypt_display, "Deactivating");
	message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_portlist->port_id, EPOINT_TO_PORT, MESSAGE_NOTIFY);
	SCPY(message->param.notifyinfo.display, "Deactivating");
	message_put(message);
	e_crypt = CRYPT_RELEASE;
	/* sending activation */
	message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_join_id, EPOINT_TO_JOIN, MESSAGE_CRYPT);
	message->param.crypt.type = CU_DACT_REQ;
	message_put(message);
}


/* messages from manager to endpoint
 */
void EndpointAppPBX::encrypt_result(int msg, char *text)
{
	struct lcr_msg *message;

	switch(msg) {
		case CU_ACTK_CONF:
		case CU_ACTS_CONF:
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) encryption now active.\n", ea_endpoint->ep_serial);
		set_tone(ea_endpoint->ep_portlist, "crypt_on");
		e_tone[0] = '\0';
		e_crypt = CRYPT_ON;
		display:
		if (text) if (text[0]) {
			SCPY(e_crypt_info, text);
			message = message_create(ea_endpoint->ep_serial, ea_endpoint->ep_portlist->port_id, EPOINT_TO_PORT, MESSAGE_NOTIFY);
			SCPY(message->param.notifyinfo.display, e_crypt_info);
			message_put(message);
		}
		break;

		case CU_ERROR_IND:
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) encryption error. (%s)\n", ea_endpoint->ep_serial, text);
		set_tone(ea_endpoint->ep_portlist, "crypt_off");
		e_tone[0] = '\0';
		e_crypt = CRYPT_OFF;
		goto display;
		break;

		case CU_DACT_CONF:
		case CU_DACT_IND:
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) encryption now off. (%s)\n", ea_endpoint->ep_serial, text);
		set_tone(ea_endpoint->ep_portlist, "crypt_off");
		e_tone[0] = '\0';
		e_crypt = CRYPT_OFF;
		goto display;
		break;

		case CU_INFO_CONF:
		case CU_INFO_IND:
		PDEBUG(DEBUG_EPOINT, "EPOINT(%d) information. (%s)\n", ea_endpoint->ep_serial, text);
		goto display;
		break;

		default:
		PERROR("EPOINT(%d) crypt manager sends us an invalid message. (type = %d)\n", ea_endpoint->ep_serial, msg);
	}
}


