Dovecot encryption plugin
=========================

Requirements
------------

* Ensure GCC and the header files for libcrypto (OpenSSL) and libxcrypt are installed.

Installation
------------

* Use `make dovecot-install` to download and build dovecot 2.2.21 in a sub-directory. It's a local
  installation and your system wont be affected.

* Type `make all` to compile the plugin.

* Find the plugin at dovecot/target/lib/dovecot/lib18_scrambler_plugin.so.

Tests
-----

* Ensure the [ruby version manager](http://rvm.io) is installed.

* Install ruby version 2.1.x with command `rvm install 2.1`.

* Install the bundler gem with `gem install bundler`.

* Install the gem bundle with `bundle install`.

All tests are written with RSpec and can be run with `make spec-all` or `bundle exec rake spec:integration`

Configuration
-------------

In order to run, the plugin needs the following configuration values (via the dovecot environment).

* `scrambler_plain_password` The plain user password. It's used to derive the hashed password to decrypt the
  private key.

* `scrambler_enabled` Can be `1` or `0`.

* `scrambler_public_key` The public key of the user. Formatted as _pem_.

* `scrambler_private_key` The encrypted private key of the user. Formatted as _pem_.

* `scrambler_private_key_salt` The salt of the hashed password that has been used to encrypt the private key.

* `scrambler_private_key_iterations` The number of iterations of the hashed password that has been used to
  encrypt the private key.

A configuration example can be found at `dovecot/configuration/dovecot-sql.conf.ext.erb`.

Migration
---------

The migration of unencrypted mailboxes has to be done by a separate tool and is _not_ part of this project.

Project
-------

Concept, design and realization by [Posteo e.K.](https://posteo.de).
The implementation was provided by [simia.tech GbR](http://simiatech.com).
An security audit has been provided by [Cure53](https://cure53.de).
