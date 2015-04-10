/*
Copyright (c) 2014-2015 The scrambler-plugin authors. All rights reserved.

On 30.4.2015 - or earlier on notice - the scrambler-plugin authors will make
this source code available under the terms of the GNU Affero General Public
License version 3.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <dovecot/lib.h>
#include <dovecot/ostream.h>
#include <dovecot/ostream-private.h>
#include <openssl/aes.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>

#include "scrambler-common.h"
#include "scrambler-ostream.h"

// Structs

struct scrambler_ostream {
	struct ostream_private ostream;

	enum packages package;

    EVP_PKEY *public_key;
    EVP_CIPHER_CTX *cipher_context;
    const EVP_CIPHER *cipher;

		unsigned char mac_key[MAC_KEY_SIZE];

		unsigned int chunk_index;
    unsigned char chunk_buffer[CHUNK_SIZE];
    unsigned int chunk_buffer_size;

		bool flushed;

#ifdef DEBUG_STREAMS
		unsigned int in_byte_count;
		unsigned int out_byte_count;
#endif
};

// Functions

static ssize_t scrambler_ostream_send_header(struct scrambler_ostream *sstream) {
    // send header and package information
    o_stream_send(sstream->ostream.parent, scrambler_header, sizeof(scrambler_header));
    o_stream_send(sstream->ostream.parent, (unsigned char *)&sstream->package, 1);
#ifdef DEBUG_STREAMS
		sstream->out_byte_count += sizeof(scrambler_header) + 1;
#endif

    sstream->cipher = scrambler_cipher(sstream->package);
    sstream->cipher_context = EVP_CIPHER_CTX_new();

    EVP_PKEY *public_keys[] = { sstream->public_key };

    int iv_size = EVP_CIPHER_iv_length(sstream->cipher);
    unsigned char iv[iv_size];

    int encrypted_key_size = EVP_PKEY_size(sstream->public_key);
    unsigned char encrypted_key[encrypted_key_size];
    unsigned char *encrypted_keys[] = { encrypted_key };

    ASSERT_OPENSSL_SUCCESS(
        EVP_SealInit(sstream->cipher_context, sstream->cipher,
            encrypted_keys, &encrypted_key_size, iv, public_keys, 1), 1,
        "scrambler_ostream_send_header", "initialization of public key encryption failed", -1);
		i_assert(encrypted_key_size == EVP_PKEY_size(sstream->public_key));

    o_stream_send(sstream->ostream.parent, iv, iv_size);
    o_stream_send(sstream->ostream.parent, encrypted_key, encrypted_key_size);
#ifdef DEBUG_STREAMS
		sstream->out_byte_count += iv_size + encrypted_key_size;
#endif

		// generate a mac key
    if (1 != RAND_bytes(sstream->mac_key, MAC_KEY_SIZE))
        return -1;

		// encrypt the mac key
		unsigned char encrypted_mac_key[MAC_KEY_SIZE];
		int encrypted_mac_key_size;
		ASSERT_OPENSSL_SUCCESS(
				EVP_SealUpdate(sstream->cipher_context,
						encrypted_mac_key, &encrypted_mac_key_size, sstream->mac_key, MAC_KEY_SIZE), 1,
				"scrambler_ostream_send_header", "mac key encryption failed", -1)
		i_assert(encrypted_mac_key_size == MAC_KEY_SIZE);
    o_stream_send(sstream->ostream.parent, encrypted_mac_key, encrypted_mac_key_size);
#ifdef DEBUG_STREAMS
		sstream->out_byte_count += encrypted_mac_key_size;
#endif

		// i_debug("write header / size = %d / parent_size = %d",
		// 		0, (int)(iv_size + encrypted_key_size + encrypted_mac_key_size));

    // i_debug_hex("mac key", sstream->mac_key, MAC_KEY_SIZE);

    return 0;
}

static ssize_t scrambler_ostream_send_chunk(
    struct scrambler_ostream *sstream,
    const unsigned char *chunk,
    size_t chunk_size,
		bool final
) {
    int encrypted_size = 0;
		unsigned short total_encrypted_size = 0;
		unsigned char encrypted[chunk_size + EVP_CIPHER_block_size(sstream->cipher) - 1];

#ifdef DEBUG_STREAMS
		// i_debug("chunk %s", chunk);
		i_debug_hex("chunk", chunk, chunk_size);
#endif

		ASSERT_OPENSSL_SUCCESS(
				EVP_SealUpdate(sstream->cipher_context, encrypted, &encrypted_size, chunk, chunk_size), 1,
				"scrambler_ostream_send_chunk", "stream encryption failed", -1)
		i_assert((size_t)encrypted_size == chunk_size);
		total_encrypted_size += encrypted_size;

		if (final) {
				encrypted_size = 0;
				ASSERT_OPENSSL_SUCCESS(
						EVP_SealFinal(sstream->cipher_context, encrypted + total_encrypted_size, &encrypted_size), 1,
						"scrambler_ostream_send_chunk", "stream finalization failed", -1)
				total_encrypted_size += encrypted_size;
		}

		unsigned short header = total_encrypted_size;
		if (final)
				header |= 0x8000; // set msb
		else
				header &= 0x7fff; // clear msb

		unsigned int tag_size;
		unsigned char tag[CHUNK_TAG_SIZE];
		const unsigned char *blocks[] = {
				(unsigned char *)&sstream->chunk_index,
				(unsigned char *)&header,
				encrypted,
				NULL
		};
		size_t block_sizes[] = {
			sizeof(unsigned int),
			sizeof(unsigned short),
			total_encrypted_size,
			0
		};

		scrambler_generate_mac(tag, &tag_size, blocks, block_sizes, sstream->mac_key, MAC_KEY_SIZE);
		i_assert(tag_size == CHUNK_TAG_SIZE);

		o_stream_send(sstream->ostream.parent, &header, sizeof(unsigned short));
		o_stream_send(sstream->ostream.parent, encrypted, total_encrypted_size);
    o_stream_send(sstream->ostream.parent, tag, tag_size);
#ifdef DEBUG_STREAMS
		sstream->out_byte_count += sizeof(unsigned short) + total_encrypted_size + tag_size;
#endif

		sstream->chunk_index++;

    return chunk_size;
}

static ssize_t scrambler_ostream_sendv(
    struct ostream_private *stream,
    const struct const_iovec *iov,
    unsigned int iov_count
) {
		struct scrambler_ostream *sstream = (struct scrambler_ostream *)stream;
		ssize_t result = 0;
    ssize_t encrypt_result = 0;

    // encrypt and send data
		unsigned int index;
    const unsigned char *source, *source_end;
    size_t chunk_size;
		for (index = 0; index < iov_count; index++) {
        source = iov[index].iov_base;
        source_end = (unsigned char *)iov[index].iov_base + iov[index].iov_len;

    		while (source < source_end) {
            chunk_size = MIN(CHUNK_SIZE, source_end - source);

            if (sstream->chunk_buffer_size > 0 || chunk_size < CHUNK_SIZE) {
                chunk_size = MIN(chunk_size, CHUNK_SIZE - sstream->chunk_buffer_size);
                memcpy(sstream->chunk_buffer + sstream->chunk_buffer_size, source, chunk_size);
                sstream->chunk_buffer_size += chunk_size;

                if (sstream->chunk_buffer_size == CHUNK_SIZE) {
									encrypt_result = scrambler_ostream_send_chunk(sstream, sstream->chunk_buffer, CHUNK_SIZE, FALSE);
                    if (encrypt_result < 0)
                        return encrypt_result;
                    sstream->chunk_buffer_size = 0;
                }
            } else {
                encrypt_result = scrambler_ostream_send_chunk(sstream, source, CHUNK_SIZE, FALSE);
                if (encrypt_result < 0)
                    return encrypt_result;
                chunk_size = encrypt_result;
            }

            source += chunk_size;
            result += chunk_size;
      	}
		}

		stream->ostream.offset += result;

#ifdef DEBUG_STREAMS
		sstream->in_byte_count += result;
		i_debug("scrambler ostream send (%d)", (int)result);
#endif

		return result;
}

static int scrambler_ostream_flush(struct ostream_private *stream) {
    struct scrambler_ostream *sstream = (struct scrambler_ostream *)stream;
    ssize_t result;

    if (sstream->flushed)
        return 0;

		if (sstream->cipher_context != NULL) {
				ssize_t result = scrambler_ostream_send_chunk(sstream, sstream->chunk_buffer, sstream->chunk_buffer_size, TRUE);
				if (result < 0) {
					i_error("error sending last chunk on close");
					return result;
				}
				sstream->chunk_buffer_size = 0;
				sstream->ostream.ostream.offset += result;

				EVP_CIPHER_CTX_free(sstream->cipher_context);
				sstream->cipher_context = NULL;
		}

    result = o_stream_flush(stream->parent);
    if (result < 0)
        o_stream_copy_error_from_parent(stream);
    else
        sstream->flushed = TRUE;

#ifdef DEBUG_STREAMS
		i_debug("scrambler ostream flush (%d)", (int)result);
#endif

		return result;
}

static void scrambler_ostream_close(struct iostream_private *stream, bool close_parent) {
    struct scrambler_ostream *sstream = (struct scrambler_ostream *)stream;

		/*
		if (sstream->cipher_context != NULL) {
				ssize_t result = scrambler_ostream_send_chunk(sstream, sstream->chunk_buffer, sstream->chunk_buffer_size, TRUE);
				if (result < 0) {
						i_error("error sending last chunk on close");
						return;
				}
				sstream->chunk_buffer_size = 0;
				sstream->ostream.ostream.offset += result;

        EVP_CIPHER_CTX_free(sstream->cipher_context);
        sstream->cipher_context = NULL;
    }
		*/

#ifdef DEBUG_STREAMS
		i_debug("scrambler ostream close - %u bytes in / %u bytes out / %u bytes overhead",
				sstream->in_byte_count, sstream->out_byte_count, sstream->out_byte_count - sstream->in_byte_count);
#endif

		if (close_parent)
	    	o_stream_close(sstream->ostream.parent);
}

struct ostream *scrambler_ostream_create(struct ostream *output, EVP_PKEY *public_key) {
    struct scrambler_ostream *sstream = i_new(struct scrambler_ostream, 1);
    struct ostream *result;

#ifdef DEBUG_STREAMS
		i_debug("scrambler ostream create");
#endif

    sstream->package = PACKAGE_RSA_2048_AES_128_CTR_HMAC;

    sstream->public_key = public_key;
    sstream->cipher_context = EVP_CIPHER_CTX_new();

		sstream->chunk_index = 0;
    sstream->chunk_buffer_size = 0;
#ifdef DEBUG_STREAMS
		sstream->in_byte_count = 0;
		sstream->out_byte_count = 0;
#endif
    sstream->flushed = FALSE;

    sstream->ostream.iostream.close = scrambler_ostream_close;
    sstream->ostream.sendv = scrambler_ostream_sendv;
    sstream->ostream.flush = scrambler_ostream_flush;

    result = o_stream_create(&sstream->ostream, output, o_stream_get_fd(output));

    if (scrambler_ostream_send_header(sstream) < 0) {
        i_error("error creating ostream");
        return NULL;
    }

    return result;
}
