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
#include "dovecot/lib.h"
#include "dovecot/array.h"
#include "dovecot/buffer.h"
#include "dovecot/hash.h"
#include "dovecot/istream.h"
#include "dovecot/ostream.h"
#include "dovecot/ostream-private.h"
#include "dovecot/str.h"
#include "dovecot/safe-mkstemp.h"
#include "dovecot/mail-user.h"
#include "dovecot/mail-storage-private.h"
#include "dovecot/index-storage.h"
#include "dovecot/index-mail.h"
#include "dovecot/strescape.h"
#include <stdio.h>

#include "scrambler-plugin.h"
#include "scrambler-common.h"
#include "scrambler-ostream.h"
#include "scrambler-istream.h"

// Defines

// After buffer grows larger than this, create a temporary file to /tmp where to read the mail.
#define MAIL_MAX_MEMORY_BUFFER (1024*128)

#define SCRAMBLER_CONTEXT(obj) \
	MODULE_CONTEXT(obj, scrambler_storage_module)
#define SCRAMBLER_MAIL_CONTEXT(obj) \
	MODULE_CONTEXT(obj, scrambler_mail_module)
#define SCRAMBLER_USER_CONTEXT(obj) \
	MODULE_CONTEXT(obj, scrambler_user_module)

// Structs

struct scrambler_user {
		union mail_user_module_context module_ctx;

    bool enabled;
    EVP_PKEY *public_key;
    EVP_PKEY *private_key;
};

const char *scrambler_plugin_version = DOVECOT_ABI_VERSION;

// Statics

static MODULE_CONTEXT_DEFINE_INIT(scrambler_storage_module, &mail_storage_module_register);
static MODULE_CONTEXT_DEFINE_INIT(scrambler_mail_module, &mail_module_register);
static MODULE_CONTEXT_DEFINE_INIT(scrambler_user_module, &mail_user_module_register);

// Functions

static const char *scrambler_get_string_setting(struct mail_user *user, const char *name) {
    return mail_user_plugin_getenv(user, name);
}

static const char *scrambler_get_pem_string_setting(struct mail_user *user, const char *name) {
		char *value = (char *)scrambler_get_string_setting(user, name);

		if (value == NULL)
				return NULL;

		scrambler_unescape_pem(value);
		return value;
}

static unsigned int scrambler_get_integer_setting(struct mail_user *user, const char *name) {
    const char *value = scrambler_get_string_setting(user, name);
    return value == NULL ? 0 : atoi(value);
}

static EVP_PKEY *scrambler_get_pem_key_setting(struct mail_user *user, const char *name) {
    const char *value = scrambler_get_pem_string_setting(user, name);

    if (value == NULL)
        return NULL;

    return scrambler_pem_read_public_key(value);
}

static void scrambler_mail_user_created(struct mail_user *user) {
    struct mail_user_vfuncs *v = user->vlast;
    struct scrambler_user *suser;

    suser = p_new(user->pool, struct scrambler_user, 1);
    suser->module_ctx.super = *v;
    user->vlast = &suser->module_ctx.super;

    suser->enabled = !!scrambler_get_integer_setting(user, "scrambler_enabled");
    suser->public_key = scrambler_get_pem_key_setting(user, "scrambler_public_key");

    const char *plain_password = scrambler_get_string_setting(user, "scrambler_plain_password");
    unsigned int plain_password_fd = scrambler_get_integer_setting(user, "scrambler_plain_password_fd");
  	const char *private_key = scrambler_get_pem_string_setting(user, "scrambler_private_key");
    const char *private_key_salt = scrambler_get_string_setting(user, "scrambler_private_key_salt");
    unsigned int private_key_iterations = scrambler_get_integer_setting(user, "scrambler_private_key_iterations");

    if (plain_password == NULL && plain_password_fd != 0) {
        plain_password = scrambler_read_line_fd(user->pool, plain_password_fd);
    }

    if (plain_password != NULL && private_key != NULL && private_key_salt != NULL) {
        const char *hashed_password = scrambler_hash_password(plain_password, private_key_salt, private_key_iterations);
        suser->private_key = scrambler_pem_read_encrypted_private_key(private_key, hashed_password);
        if (suser->private_key == NULL) {
            user->error = p_strdup_printf(user->pool,
                "Failed to load and decrypt the private key. May caused by an invalid password.");
        }
    } else {
        suser->private_key = NULL;
    }

    MODULE_CONTEXT_SET(user, scrambler_user_module, suser);
}

static int scrambler_mail_save_begin(struct mail_save_context *context, struct istream *input) {
    struct mailbox *box = context->transaction->box;
    struct scrambler_user *suser = SCRAMBLER_USER_CONTEXT(box->storage->user);
    union mailbox_module_context *mbox = SCRAMBLER_CONTEXT(box);
    struct ostream *output;

    if (suser->enabled && suser->public_key == NULL) {
        i_error("scrambler_mail_save_begin: encryption is enabled, but no public key is available");
        return -1;
    }

		if (mbox->super.save_begin(context, input) < 0)
				return -1;

    if (suser->enabled) {
			  // TODO: find a better solution for this. this currently works, because
				// there is only one other ostream (zlib) in the setup. the scrambler should
				// be added to the other end of the ostream chain, not to the
				// beginning (the usual way).
				if (context->data.output->real_stream->parent == NULL) {
						output = scrambler_ostream_create(context->data.output, suser->public_key);
						o_stream_unref(&context->data.output);
						context->data.output = output;
				} else {
						output = scrambler_ostream_create(context->data.output->real_stream->parent, suser->public_key);
						o_stream_unref(&context->data.output->real_stream->parent);
						context->data.output->real_stream->parent = output;
				}
#ifdef DEBUG_STREAMS
        i_debug("scrambler write encrypted mail");
    } else {
        i_debug("scrambler write plain mail");
#endif
    }

    return 0;
}

static void scrambler_mailbox_allocated(struct mailbox *box) {
    struct mailbox_vfuncs *v = box->vlast;
    union mailbox_module_context *mbox;
    enum mail_storage_class_flags class_flags = box->storage->class_flags;

    mbox = p_new(box->pool, union mailbox_module_context, 1);
    mbox->super = *v;
    box->vlast = &mbox->super;

    MODULE_CONTEXT_SET_SELF(box, scrambler_storage_module, mbox);

    if ((class_flags & MAIL_STORAGE_CLASS_FLAG_OPEN_STREAMS) == 0)
        v->save_begin = scrambler_mail_save_begin;
}

static int scrambler_istream_opened(struct mail *_mail, struct istream **stream) {
    struct mail_private *mail = (struct mail_private *)_mail;
    struct mail_user *user = _mail->box->storage->user;
    struct scrambler_user *suser = SCRAMBLER_USER_CONTEXT(user);
    union mail_module_context *mmail = SCRAMBLER_MAIL_CONTEXT(mail);
    struct istream *input;

    input = *stream;
    *stream = scrambler_istream_create(input, suser->private_key);
    i_stream_unref(&input);

		int result = mmail->super.istream_opened(_mail, stream);

    return result;
}

static void scrambler_mail_allocated(struct mail *_mail) {
		struct mail_private *mail = (struct mail_private *)_mail;
		struct mail_vfuncs *v = mail->vlast;
		union mail_module_context *mmail;

		mmail = p_new(mail->pool, union mail_module_context, 1);
		mmail->super = *v;
		mail->vlast = &mmail->super;

		v->istream_opened = scrambler_istream_opened;

		MODULE_CONTEXT_SET_SELF(mail, scrambler_mail_module, mmail);
}

static struct mail_storage_hooks scrambler_mail_storage_hooks = {
		.mail_user_created = scrambler_mail_user_created,
		.mailbox_allocated = scrambler_mailbox_allocated,
		.mail_allocated = scrambler_mail_allocated
};

void scrambler_plugin_init(struct module *module) {
    scrambler_initialize();
		mail_storage_hooks_add(module, &scrambler_mail_storage_hooks);
}

void scrambler_plugin_deinit(void) {
		mail_storage_hooks_remove(&scrambler_mail_storage_hooks);
}
