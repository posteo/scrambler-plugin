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
#include <dovecot/base64.h>
#include <dovecot/buffer.h>
#include <dovecot/str.h>
#include <unistd.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/pem.h>
#include <xcrypt.h>
#include <errno.h>
#include <string.h>

#include "scrambler-common.h"

// Constants

const char scrambler_header[] = { 0xee, 0xff, 0xcc };

// Functions

void scrambler_initialize(void) {
    OpenSSL_add_all_algorithms();
    ERR_load_crypto_strings();
    i_info("scrambler plugin initialized");
}

const char *scrambler_read_line_fd(pool_t pool, int fd) {
    string_t *buffer = str_new(pool, MAXIMAL_PASSWORD_LENGTH);
    char *result = str_c_modifiable(buffer);
    char *pointer = result;

    ssize_t read_result = read(fd, pointer, 1);
    unsigned int bytes_read = 0;
    while (read_result != -1 && pointer[0] != '\n') {
        pointer++;
        bytes_read++;

        if (bytes_read > MAXIMAL_PASSWORD_LENGTH) {
            i_error("error reading form fd %d: password too long", fd);
            break;
        }

        read_result = read(fd, pointer, 1);
    }

    pointer[0] = 0;

    if (read_result == -1)
        i_error("error reading from fd %d: %s (%d)", fd, strerror(errno), errno);

    return result;
}

const char *scrambler_hash_password(
    const char *password,
    const char *salt,
    unsigned int iterations
) {
    size_t salt_size = strlen(salt);
    char settings[30];

    if (iterations < 4 || iterations > 31) {
        i_error("scrambler_hash_password: iterations must be between 4 and 31 (inclusive), current value is %u",
                iterations);
        return NULL;
    }

    if (salt_size != 22) {
        i_error("scrambler_hash_password: salt must have a size of 22, current size is %u", (unsigned int)salt_size);
        return NULL;
    }

    sprintf(settings, "$2a$%02u$%22s", iterations, salt);

    return crypt(password, settings);
}

const EVP_CIPHER *scrambler_cipher(enum packages package) {
    switch (package) {
    case PACKAGE_RSA_2048_AES_128_CTR_HMAC:
        return EVP_aes_128_ctr();
    }
    return NULL;
}

void scrambler_generate_mac(
    unsigned char *tag, unsigned int *tag_size,
    const unsigned char *sources[], size_t source_sizes[],
    const unsigned char *key, size_t key_size
) {
    HMAC_CTX context;
    HMAC_CTX_init(&context);
    HMAC_Init_ex(&context, key, key_size, EVP_sha256(), NULL);

    unsigned int index = 0;
    const unsigned char *source = sources[index];
    size_t source_size = source_sizes[index];
    while (source != NULL) {
        HMAC_Update(&context, source, source_size);

        index++;
        source = sources[index];
        source_size = source_sizes[index];
    }

    HMAC_Final(&context, tag, tag_size);

    HMAC_CTX_cleanup(&context);
}

void scrambler_unescape_pem(char *pem) {
    while (*pem != '\0') {
        if (*pem == '_')
            *pem = '\n';

        pem++;
    }
}

EVP_PKEY *scrambler_pem_read_public_key(const char *source) {
    BIO *public_key_pem_bio = BIO_new_mem_buf((char *)source, -1);
    EVP_PKEY *result = PEM_read_bio_PUBKEY(public_key_pem_bio, NULL, NULL, NULL);
    BIO_free_all(public_key_pem_bio);

    if (result == NULL)
        i_error_openssl("scrambler_pem_read_public_key");
    return result;
}

EVP_PKEY *scrambler_pem_read_encrypted_private_key(const char *source, const char *password) {
    BIO *private_key_pem_bio = BIO_new_mem_buf((char *)source, -1);
    EVP_PKEY *result = PEM_read_bio_PrivateKey(private_key_pem_bio, NULL, NULL, (void *)password);
    BIO_free_all(private_key_pem_bio);

    if (result == NULL)
        i_error_openssl("scrambler_pem_read_encrypted_private_key");
    return result;
}

void i_error_openssl(const char *function_name) {
    char *output;
    BIO *output_bio = BIO_new(BIO_s_mem());
    ERR_print_errors(output_bio);
    BIO_get_mem_data(output_bio, &output);

    i_error("%s: %s", function_name, output);

    BIO_free_all(output_bio);
}

void i_debug_hex(const char *prefix, const unsigned char *data, size_t size) {
    T_BEGIN {
        string_t *output = t_str_new(1024);
        str_append(output, prefix);
        str_append(output, ": ");
        for (size_t index = 0; index < size; index++) {
            if (index > 0)
                str_append(output, " ");

            str_printfa(output, "%02x", data[index]);
        }
        i_debug("%s", str_c(output));
    } T_END;
}
