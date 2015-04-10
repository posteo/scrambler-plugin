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
#ifndef SCRAMBLER_ISTREAM_H
#define SCRAMBLER_ISTREAM_H

#include <openssl/evp.h>

struct istream *scrambler_istream_create(struct istream *input, EVP_PKEY *private_key);

#endif
