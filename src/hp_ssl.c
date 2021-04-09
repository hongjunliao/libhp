/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2017/12/28
 *
 * libssl rsa
 *
 * sample code from
 * https://shanetully.com/2012/04/simple-public-key-encryption-with-rsa-and-openssl/
 * http://www.ioncannon.net/programming/34/howto-base64-encode-with-cc-and-openssl/
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef LIBHP_WITH_SSL

#include <openssl/rsa.h>
#include "openssl/pem.h"
#include "openssl/err.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "str_dump.h"   /* dumpstr */
#include "hp_ssl.h"

/////////////////////////////////////////////////////////////////////////////////////////
#ifndef NDEBUG

static char const * oauth2RSAPublicKey =
"-----BEGIN PUBLIC KEY-----\n"
"MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQCkh2hS0KYiw+JMsAzy2UJ4QGSs\n"
"x1gkPwrI7bhVRWYgd1gSTm37n9tg0In7EVkoqSoPw6zFV+9C0y52pYKSMdJrbThX\n"
"waENUYBxTX0UzQRX21AgPBkmx1r2BYQ9SeT/jMwRrW2N+iReDbMfCCxJaLRutz3/\n"
"XilkavbSX8yixkQjNQIDAQAB\n"
"-----END PUBLIC KEY-----";

static char const * oauth2RSAPrivateKey =
"-----BEGIN RSA PRIVATE KEY-----\n"
"MIICdwIBADANBgkqhkiG9w0BAQEFAASCAmEwggJdAgEAAoGBAKSHaFLQpiLD4kywD\n"
"PLZQnhAZKzHWCQ/CsjtuFVFZiB3WBJObfuf22DQifsRWSipKg/DrMVX70LTLnalgp\n"
"Ix0mttOFfBoQ1RgHFNfRTNBFfbUCA8GSbHWvYFhD1J5P+MzBGtbY36JF4Nsx8ILEl\n"
"otG63Pf9eKWRq9tJfzKLGRCM1AgMBAAECgYAkA+gYSMg1T//XnaoX9usP+7iOAc0P\n"
"kiVAOplhQSHL9ZP33edBb4rMNJoftXp45h7o+IJ3aHpdfHDtU+mzKujOdVcB4CYiH\n"
"U+1ghyiJ+KHCZ6VqtcAndHP75/gkPNH4t/qN2EFVv7TezsewLf1LIvBlZcV0f12nn\n"
"Y9lpaiF1kJeQJBANaucDmxpDh6tUGju4192msu8gz3CNy/llUsf6yLHhmBlii4qNZ\n"
"J3fDNnPWSxeyYbp26gvgeh1XhJDjPxuEntBMCQQDEMeiLnG2Rnrhovvgp68uzxIsh\n"
"KrEn/CrzMTp6IN6p71kXOaWTs3vB/CKtg764X0XG6ppk6cTHewmJvH+1qOSXAkEAh\n"
"mqOJfGOCzb5inHEGuF0AqxQLcH3MJBcxlOoVRZ98CZtKG4GeLWjWwTChBg0COGgUO\n"
"3Y1xX2UtU24sNlmNBNNQJBALTnRAI/SbSFAorq045r8lce+h6p69HvrXayRLZJyqY\n"
"soRxONkNbsthqcVtG6Du+9Wr19UjpWF2LMH9FRQiu458CQHnr4U2HeCXMlJ8JBTn/\n"
"USqJqAlrDJKLj8RVpK4x/tthU/JV+CZ+1a035PsLz5vN4R2AVgkY+63u0JL1HTw6L\n"
"rE=\n"
"-----END RSA PRIVATE KEY-----";
#endif /* NDEBUG */

#define KEY_LENGTH  1024
#define PUB_EXP     3

/*
 * @param msg: Message to encrypt
 * NOTE:
 * (1)strlen(msg) < 128
 * (2)call free(outbuf) after unused
 * */
int hp_ssl_rsa128(char const * pubkey, char const * msg, char ** outbuf)
{
	if(!(pubkey && msg && outbuf))
		return -1;

	*outbuf = 0;

	int ret = 0;

	BIO * bufio = 0;
	RSA * keypair = 0; 	     // = RSA_generate_key(KEY_LENGTH, PUB_EXP, NULL, NULL);
	char *encrypt = NULL;    // Encrypted message
	char err[130] = "";      // Buffer for any error messages
	int encrypt_len;

	bufio = BIO_new_mem_buf(pubkey, strlen(pubkey));
	if(!bufio){
		fprintf(stderr, "%s: BIO_new_mem_buf failed\n", __FUNCTION__);
		ret = -2;
		goto free_stuff;
	}

//	keypair = RSA_generate_key(KEY_LENGTH, PUB_EXP, NULL, NULL);
	keypair = PEM_read_bio_RSA_PUBKEY(bufio, &keypair, 0, 0);
	if(!keypair){
		fprintf(stderr, "%s/%d: PEM_read_bio_RSA_PUBKEY failed\n", __FUNCTION__, __LINE__);
		ret = -3;
		goto free_stuff;
	}

	encrypt = malloc(RSA_size(keypair));
	if ((encrypt_len = RSA_public_encrypt(strlen(msg) + 1, (unsigned char*) msg,
			(unsigned char*) encrypt, keypair, RSA_PKCS1_PADDING)) == -1) {
		ERR_load_crypto_strings();
		ERR_error_string(ERR_get_error(), err);

		fprintf(stderr, "%s: Error encrypting message: %s\n", __FUNCTION__, err);
		ret = -4;
		goto free_stuff;
	}

#ifndef NDEBUG
	if(strcmp(pubkey, oauth2RSAPublicKey) == 0){
		FILE *out = fopen("/tmp/hp_ssl_rsa128.en", "w");
		if(fwrite(encrypt, sizeof(*encrypt),  RSA_size(keypair), out) <= 0)
			fprintf(stderr, "%s/%d: Encrypted message written to file failed, errno=%d, error='%s'\n"
					, __FUNCTION__, __LINE__, errno, strerror(errno));
		fclose(out);
		// Decrypt it
		{BIO * bufiopri = BIO_new_mem_buf(oauth2RSAPrivateKey, strlen(oauth2RSAPrivateKey));
		if(!bufiopri){
			fprintf(stderr, "%s: BIO_new_mem_buf failed\n", __FUNCTION__);
			ret = -5;
			goto dbg_free_stuff;
		}

		keypair = PEM_read_bio_RSAPrivateKey(bufiopri, &keypair, 0, 0);
		if(!keypair){
			fprintf(stderr, "%s/%d: PEM_read_bio_RSA_PUBKEY failed\n", __FUNCTION__, __LINE__);
			ret = -6;
			goto dbg_free_stuff;
		}
		char *decrypt = NULL;    // Decrypted message
		decrypt = malloc(encrypt_len);
		if (RSA_private_decrypt(encrypt_len, (unsigned char*) encrypt,
				(unsigned char*) decrypt, keypair, RSA_PKCS1_PADDING) == -1) {
			ERR_load_crypto_strings();
			ERR_error_string(ERR_get_error(), err);
			fprintf(stderr, "%s: Error decrypting message: %s\n", __FUNCTION__, err);

			ret = -5;
			goto dbg_free_stuff;
		}
		printf("%s: Decrypted message: '%s', encrypt_len=%d\n"
				, __FUNCTION__, dumpstr(decrypt, encrypt_len, 64), encrypt_len);
	dbg_free_stuff:
		free(decrypt);
		BIO_free_all(bufiopri);}
	}
#endif /* NDEBUG */

	*outbuf = encrypt;
free_stuff:
	BIO_free_all(bufio);
	RSA_free(keypair);

	return ret;
}

/*
 * NOTE:
 * call free(@return) after unused
 * */
char * hp_ssl_base64(const unsigned char *input, int length)
{
	BIO *bmem, *b64;
	BUF_MEM *bptr;

	b64 = BIO_new(BIO_f_base64());
	BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);

	bmem = BIO_new(BIO_s_mem());
	b64 = BIO_push(b64, bmem);
	BIO_write(b64, input, length);
	BIO_flush(b64);
	BIO_get_mem_ptr(b64, &bptr);

	char *buff = (char *) malloc(bptr->length + 1);
	memcpy(buff, bptr->data, bptr->length);
	buff[bptr->length] = 0;

	BIO_free_all(b64);

	return buff;
}

/////////////////////////////////////////////////////////////////////////////////////
#ifndef NDEBUG

int test_hp_ssl_main(int argc, char ** argv)
{
	char const * data = "hello";
	if(argc > 1){
		int i = 1;
		for(; i < argc; ++i){
			if(strncmp(argv[i], "-I", 2) == 0)
				data = argv[i] + 2;
			else if (strncmp(argv[i], "--in", 5) == 0)
				data = argv[i] + 5;
		}
	}

	char * out = 0;
	int r = hp_ssl_rsa128(oauth2RSAPublicKey, data, &out);
	assert(r == 0 && out);
	free(out);
	//////////////////////////////////////

	data = "{\"userName\":\"gabtest\",\"password\":\"123456\"}";
	out = hp_ssl_base64((unsigned char *)data, strlen(data));
	fprintf(stdout, "__out='%s', dump='%s'__\n", out, dumpstr(out, strlen(out), strlen(out)));
	assert(out && strcmp(out, "eyJ1c2VyTmFtZSI6ImdhYnRlc3QiLCJwYXNzd29yZCI6IjEyMzQ1NiJ9") == 0);
	free(out);
	//////////////////////////////////////

	data = "{\"userName\":\"gabtest\",\"password\":\"123456\"}\n\0";
	out = hp_ssl_base64((unsigned char *)data, strlen(data));
	fprintf(stdout, "__out='%s', dump='%s'__\n", out, dumpstr(out, strlen(out), strlen(out)));
	assert(out && strcmp(out, "eyJ1c2VyTmFtZSI6ImdhYnRlc3QiLCJwYXNzd29yZCI6IjEyMzQ1NiJ9Cg==") == 0);
	free(out);
	//////////////////////////////////////
	return r;
}

#endif /* NDEBUG */
#endif /* LIBHP_WITH_SSL */

