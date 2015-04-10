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
#include <dovecot/istream.h>
#include <dovecot/istream-private.h>
#include <openssl/aes.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#include "scrambler-common.h"
#include "scrambler-istream.h"

// Enums

enum scrambler_istream_mode {
    detect,
    decrypt,
    plain
};

// Structs

struct scrambler_istream {
	struct istream_private istream;

    enum scrambler_istream_mode mode;

    EVP_PKEY *private_key;
    EVP_CIPHER_CTX *cipher_context;
    const EVP_CIPHER *cipher;
    unsigned int encrypted_header_size;

    unsigned char mac_key[MAC_KEY_SIZE];

    unsigned int chunk_index;
    bool last_chunk_read;

#ifdef DEBUG_STREAMS
    unsigned int in_byte_count;
    unsigned int out_byte_count;
#endif
};

// Functions

static ssize_t scrambler_istream_read_parent(
    struct scrambler_istream *sstream,
    size_t minimal_read_size,
    size_t minimal_alloc_size
) {
    struct istream_private *stream = &sstream->istream;
    size_t size;
    ssize_t result;

    size = i_stream_get_data_size(stream->parent);
    while (minimal_read_size != 0 && size < minimal_read_size) {
        result = i_stream_read(stream->parent);
        size = i_stream_get_data_size(stream->parent);

        if (result > 0 && stream->parent->eof)
            break;

        if (result <= 0 && (result != -2 || stream->skip == 0)) {
            stream->istream.stream_errno = stream->parent->stream_errno;
            stream->istream.eof = stream->parent->eof;
            return result;
        }
    }

    i_stream_alloc(stream, MAX(minimal_alloc_size, size));
    return size;
}

static ssize_t scrambler_istream_read_detect_magic(
    struct scrambler_istream *sstream,
    const unsigned char *source
) {
    // read header and package information
    if (0 == memcmp(scrambler_header, source, sizeof(scrambler_header))) {
#ifdef DEBUG_STREAMS
        i_debug("istream read encrypted mail");
#endif
        sstream->mode = decrypt;
        if (sstream->private_key == NULL) {
            i_error("tried to decrypt a mail without the private key");
            sstream->istream.istream.stream_errno = EACCES;
            sstream->istream.istream.eof = TRUE;
            return -1;
        } else {
            source += sizeof(scrambler_header);
            enum packages package = *source;
            sstream->cipher = scrambler_cipher(package);
            if (sstream->cipher == NULL) {
                i_error("could not detect encryption package signature (%02x)", package);
                sstream->istream.istream.stream_errno = EACCES;
                sstream->istream.istream.eof = TRUE;
                return -1;
            }

            sstream->encrypted_header_size =
                EVP_CIPHER_iv_length(sstream->cipher) +
                EVP_PKEY_size(sstream->private_key) +
                MAC_KEY_SIZE;

            return MAGIC_SIZE;
        }
    } else {
#ifdef DEBUG_STREAMS
        i_debug("istream read plain mail");
#endif
        sstream->mode = plain;
        return 0;
    }
}

static ssize_t scrambler_istream_read_detect(struct scrambler_istream *sstream) {
    struct istream_private *stream = &sstream->istream;
    const unsigned char *source;
    ssize_t result;
    size_t source_size;

    i_stream_set_max_buffer_size(sstream->istream.parent, ENCRYPTED_HEADER_SIZE + (2 * ENCRYPTED_CHUNK_SIZE));

    result = scrambler_istream_read_parent(sstream, MAGIC_SIZE, 0);
    if (result <= 0)
        return result;
    source = i_stream_get_data(stream->parent, &source_size);

    result = scrambler_istream_read_detect_magic(sstream, source);
    if (result < 0)
        return result;
#ifdef DEBUG_STREAMS
    sstream->in_byte_count += result;
#endif

    i_stream_skip(stream->parent, result);
    return result;
}

static ssize_t scrambler_istream_read_decrypt_header(
    struct scrambler_istream *sstream,
    const unsigned char **source
) {
    sstream->cipher_context = EVP_CIPHER_CTX_new();

    size_t iv_size = EVP_CIPHER_iv_length(sstream->cipher);
    unsigned char iv[iv_size];
    memcpy(iv, *source, iv_size);
    *source += iv_size;
#ifdef DEBUG_STREAMS
    sstream->in_byte_count += iv_size;
#endif

    size_t encrypted_key_size = EVP_PKEY_size(sstream->private_key);
    ASSERT_OPENSSL_SUCCESS(
        EVP_OpenInit(sstream->cipher_context, sstream->cipher, *source, encrypted_key_size, iv, sstream->private_key), 1,
        "scrambler_istream_read_decrypt_header", "initialization of public key decryption failed", -1);
    *source += encrypted_key_size;
#ifdef DEBUG_STREAMS
    sstream->in_byte_count += encrypted_key_size;
#endif

    // decrypt mac key
    int decrypted_mac_key_size;
    ASSERT_OPENSSL_SUCCESS(
        EVP_OpenUpdate(sstream->cipher_context, sstream->mac_key, &decrypted_mac_key_size, *source, MAC_KEY_SIZE), 1,
        "scrambler_istream_read_decrypt_header", "mac key decryption failed", -1)
    i_assert(decrypted_mac_key_size == MAC_KEY_SIZE);
    *source += MAC_KEY_SIZE;
#ifdef DEBUG_STREAMS
    sstream->in_byte_count += MAC_KEY_SIZE;
#endif

    return 0;
}

static ssize_t scrambler_istream_read_decrypt_chunk(
    struct scrambler_istream *sstream,
    unsigned char **destination,
    const unsigned char **source,
    const unsigned char *source_end
) {
    int decrypted_size = 0;
    int total_decrypted_size = 0;

    const unsigned char *header = *source;
    unsigned short encrypted_size = *(unsigned short *)header;
    bool final = (encrypted_size & 0x8000) != 0;
    encrypted_size &= 0x7fff; // clear msb
    *source += sizeof(unsigned short);
#ifdef DEBUG_STREAMS
    sstream->in_byte_count += sizeof(unsigned short);
#endif

    if (*source + encrypted_size + CHUNK_TAG_SIZE > source_end) {
        i_error("failed to verify chunk size");
        sstream->istream.istream.stream_errno = EIO;
        sstream->istream.istream.eof = TRUE;
        return -1;
    }

    const unsigned char *encrypted = *source;
    *source += encrypted_size;
#ifdef DEBUG_STREAMS
    sstream->in_byte_count += encrypted_size;
#endif

    const unsigned char *tag = *source;
    *source += CHUNK_TAG_SIZE;
#ifdef DEBUG_STREAMS
    sstream->in_byte_count += CHUNK_TAG_SIZE;
#endif

    // verify the mac
    unsigned int generated_tag_size;
    unsigned char generated_tag[CHUNK_TAG_SIZE];
    const unsigned char *blocks[] = {
        (unsigned char *)&sstream->chunk_index,
        (unsigned char *)header,
        encrypted,
        NULL
    };
    size_t block_sizes[] = {
        sizeof(unsigned int),
        sizeof(unsigned short),
        encrypted_size,
        0
    };

    scrambler_generate_mac(generated_tag, &generated_tag_size, blocks, block_sizes, sstream->mac_key, MAC_KEY_SIZE);
    i_assert(generated_tag_size == CHUNK_TAG_SIZE);

    if (CRYPTO_memcmp(tag, generated_tag, generated_tag_size)) {
        i_error("failed to verify chunk tag");
        sstream->istream.istream.stream_errno = EACCES;
        sstream->istream.istream.eof = TRUE;
        return -1;
    }

    // decrypt
    ASSERT_OPENSSL_SUCCESS(
        EVP_OpenUpdate(sstream->cipher_context, *destination, &decrypted_size, encrypted, encrypted_size), 1,
        "scrambler_istream_read_decrypt_chunk", "stream decryption failed", -1)
    i_assert(decrypted_size == (int)encrypted_size);
    total_decrypted_size += decrypted_size;
    *destination += decrypted_size;

    if (final) {
        ASSERT_OPENSSL_SUCCESS(
            EVP_OpenFinal(sstream->cipher_context, *destination, &decrypted_size), 1,
            "scrambler_istream_read_decrypt_chunk", "stream finalization failed", -1)
        total_decrypted_size += decrypted_size;
        *destination += decrypted_size;
    }

    sstream->chunk_index++;

#ifdef DEBUG_STREAMS
    i_debug_hex("chunk", *destination - total_decrypted_size, total_decrypted_size);
#endif

    return 0;
}

static ssize_t scrambler_istream_read_decrypt(struct scrambler_istream *sstream) {
    struct istream_private *stream = &sstream->istream;
    const unsigned char *parent_data, *source, *source_end;
    unsigned char *destination, *destination_end;
    ssize_t result;
    size_t source_size, minimal_size;

    minimal_size = sstream->cipher_context == NULL ? sstream->encrypted_header_size : 0;
    minimal_size += ENCRYPTED_CHUNK_SIZE;

    result = scrambler_istream_read_parent(sstream, minimal_size, CHUNK_SIZE + stream->pos);
    if (result <= 0 && result != -1)
        return result;

    parent_data = i_stream_get_data(stream->parent, &source_size);
    source = parent_data;
    source_end = source + source_size;
    destination = stream->w_buffer + stream->pos;
    destination_end = stream->w_buffer + stream->buffer_size;

    // handle header and chiper initialization
    if (sstream->cipher_context == NULL) {
        result = scrambler_istream_read_decrypt_header(sstream, &source);
        if (result < 0) {
            stream->istream.stream_errno = EIO;
            return result;
        }
    }

    while ( (source_end - source) >= ENCRYPTED_CHUNK_SIZE ) {
        if (destination_end - destination < CHUNK_SIZE) {
            i_error("output buffer too small");
            sstream->istream.istream.stream_errno = EIO;
            sstream->istream.istream.eof = TRUE;
            return -1;
        }

        result = scrambler_istream_read_decrypt_chunk(sstream, &destination, &source, source_end);
        if (result < 0)
            return result;
    }

    if (stream->parent->eof) {
        if (sstream->last_chunk_read) {
            stream->istream.stream_errno = stream->parent->stream_errno;
            stream->istream.eof = stream->parent->eof;
            return -1;
        } else {
            stream->istream.stream_errno = 0;
            stream->istream.eof = FALSE;

            if (destination_end - destination < CHUNK_SIZE) {
                i_error("output buffer too small (for final chunk)");
                sstream->istream.istream.stream_errno = EIO;
                sstream->istream.istream.eof = TRUE;
                return -1;
            }

            result = scrambler_istream_read_decrypt_chunk(sstream, &destination, &source, source_end);
            if (result < 0) {
                stream->istream.stream_errno = EIO;
                return result;
            }

            sstream->last_chunk_read = TRUE;
        }
    }

    i_stream_skip(stream->parent, source - parent_data);

    result = (destination - stream->w_buffer) - stream->pos;
    stream->pos = destination - stream->w_buffer;

    if (result == 0) {
        stream->istream.stream_errno = stream->parent->stream_errno;
        stream->istream.eof = stream->parent->eof;
        return -1;
    }

#ifdef DEBUG_STREAMS
    sstream->out_byte_count += result;
    i_debug("scrambler istream read (%d)", (int)result);
#endif

    return result;
}

static ssize_t scrambler_istream_read_plain(struct scrambler_istream *sstream) {
    struct istream_private *stream = &sstream->istream;
    const unsigned char *source;
    ssize_t result;
    size_t source_size, copy_size;

    result = scrambler_istream_read_parent(sstream, 1, 0);
    if (result <= 0)
        return result;

    source = i_stream_get_data(stream->parent, &source_size);
    copy_size = MIN(source_size, stream->buffer_size - stream->pos);
    memcpy(stream->w_buffer + stream->pos, source, copy_size);

    i_stream_skip(stream->parent, copy_size);
    stream->pos += copy_size;

#ifdef DEBUG_STREAMS
    sstream->in_byte_count += copy_size;
    sstream->out_byte_count += copy_size;
#endif

    return copy_size;
}

static ssize_t scrambler_istream_read(struct istream_private *stream) {
    struct scrambler_istream *sstream = (struct scrambler_istream *)stream;

    if (sstream->mode == detect) {
        ssize_t result = scrambler_istream_read_detect(sstream);
        if (result < 0)
            return result;
    }

    if (sstream->mode == decrypt)
        return scrambler_istream_read_decrypt(sstream);

    if (sstream->mode == plain)
        return scrambler_istream_read_plain(sstream);

    return -1;
}

static void scrambler_istream_seek(struct istream_private *stream, uoff_t v_offset, bool mark) {
    struct scrambler_istream *sstream = (struct scrambler_istream *)stream;

#ifdef DEBUG_STREAMS
    i_debug("scrambler istream seek %d / %d / %d", (int)stream->istream.v_offset, (int)v_offset, (int)mark);
#endif

    if (v_offset < stream->istream.v_offset) {
        // seeking backwards - go back to beginning and seek forward from there.
        if (sstream->cipher_context != NULL) {
            EVP_CIPHER_CTX_free(sstream->cipher_context);
            sstream->cipher_context = NULL;
        }

        sstream->mode = detect;

        sstream->chunk_index = 0;
        sstream->last_chunk_read = FALSE;
#ifdef DEBUG_STREAMS
        sstream->in_byte_count = 0;
        sstream->out_byte_count = 0;
#endif

        stream->parent_expected_offset = stream->parent_start_offset;
        stream->skip = stream->pos = 0;
        stream->istream.v_offset = 0;

        i_stream_seek(stream->parent, 0);
    }
    i_stream_default_seek_nonseekable(stream, v_offset, mark);
}

static int scrambler_istream_stat(struct istream_private *stream, bool exact) {
    const struct stat *stat;

    if (i_stream_stat(stream->parent, exact, &stat) < 0)
        return -1;

    stream->statbuf = *stat;
    return 0;
}

static void scrambler_istream_close(struct iostream_private *stream, bool close_parent) {
    struct scrambler_istream *sstream = (struct scrambler_istream *)stream;

    if (sstream->cipher_context != NULL) {
        EVP_CIPHER_CTX_free(sstream->cipher_context);
        sstream->cipher_context = NULL;
    }

#ifdef DEBUG_STREAMS
    i_debug("scrambler istream close - %u bytes in / %u bytes out / %u bytes overhead",
        sstream->in_byte_count, sstream->out_byte_count, sstream->in_byte_count - sstream->out_byte_count);
#endif

    if (close_parent)
        i_stream_close(sstream->istream.parent);
}

struct istream *scrambler_istream_create(struct istream *input, EVP_PKEY *private_key) {
    struct scrambler_istream *sstream = i_new(struct scrambler_istream, 1);

#ifdef DEBUG_STREAMS
    i_debug("scrambler istream create");
#endif

    sstream->mode = detect;

    sstream->private_key = private_key;
    sstream->cipher_context = NULL;

    sstream->chunk_index = 0;
    sstream->last_chunk_read = FALSE;
#ifdef DEBUG_STREAMS
    sstream->in_byte_count = 0;
    sstream->out_byte_count = 0;
#endif

    sstream->istream.iostream.close = scrambler_istream_close;
    sstream->istream.max_buffer_size = input->real_stream->max_buffer_size;
    sstream->istream.read = scrambler_istream_read;
    sstream->istream.seek = scrambler_istream_seek;
    sstream->istream.stat = scrambler_istream_stat;

    sstream->istream.istream.readable_fd = FALSE;
    sstream->istream.istream.blocking = input->blocking;
    sstream->istream.istream.seekable = input->seekable;

    return i_stream_create(&sstream->istream, input, i_stream_get_fd(input));
}
