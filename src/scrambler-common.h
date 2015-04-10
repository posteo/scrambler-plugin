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
#ifndef SCRAMBLER_COMMON_H
#define SCRAMBLER_COMMON_H

#include <openssl/evp.h>

// Defines

#define MAGIC_SIZE (sizeof(scrambler_header) + 1)
#define ENCRYPTED_HEADER_SIZE (304)
#define CHUNK_SIZE (8192)
#define CHUNK_TAG_SIZE (32)
#define ENCRYPTED_CHUNK_SIZE ((int)sizeof(unsigned short) + CHUNK_SIZE + CHUNK_TAG_SIZE)
#define MAC_KEY_SIZE (32)
#define MAXIMAL_PASSWORD_LENGTH (256)

#define ASSERT_SUCCESS(command, expected_result, function_name, error_text, error_result) \
    if ((expected_result) != (command)) { \
        i_error("%s: %s", function_name, error_text); \
        return (error_result); \
    }

#define ASSERT_OPENSSL_SUCCESS(command, expected_result, function_name, error_text, error_result) \
    if ((expected_result) != (command)) { \
        i_error("%s: %s", function_name, error_text); \
        i_error_openssl(function_name); \
        return (error_result); \
    }

#define MIN(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })

#define MAX(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

// Enums

enum packages {
    PACKAGE_RSA_2048_AES_128_CTR_HMAC = 0x00
};

// Constants
const char scrambler_header[3];

// Functions

void scrambler_initialize(void);

const char *scrambler_read_line_fd(pool_t pool, int file_descriptor);

const char *scrambler_hash_password(
    const char *password,
    const char *salt,
    unsigned int iterations);

const EVP_CIPHER *scrambler_cipher(enum packages package);

void scrambler_generate_mac(
  unsigned char *tag, unsigned int *tag_size,
  const unsigned char *sources[], size_t source_sizes[],
  const unsigned char *key, size_t key_size);

void scrambler_unescape_pem(char *source);

EVP_PKEY *scrambler_pem_read_public_key(const char *source);

EVP_PKEY *scrambler_pem_read_encrypted_private_key(const char *source, const char *password);

void i_error_openssl(const char *function_name);

void i_debug_hex(const char *prefix, const unsigned char *data, size_t size);

#endif
