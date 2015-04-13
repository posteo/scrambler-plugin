Dovecot encryption plugin
=========================

Requirements
------------

* Ensure GCC and the header files for libcrypto (OpenSSL) and libxcrypt are installed.

Installation
------------

* Use `make dovecot-install` to download and build dovecot 2.2.15 in a sub-directory. It's a local
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

Since dovecot needs root privileges to run, you need create the file `sudo.password` with contains the
password that is needed for running dovecot with sudo.

Project
-------

Concept, design and realization by [Posteo e.K.](https://posteo.de).
The implementation was provided by [simia.tech GbR](http://simiatech.com).
An security audit has been provided by [Cure53](https://cure53.de).
