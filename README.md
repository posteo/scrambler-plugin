Dovecot encryption plugin
=========================

Requirements
------------

* Dovecot version 2.2.15 was used to build the plug in.

Installation
------------

* Ensure GCC, libcrypto (OpenSSL) and libxcrypt are installed.

* Ensure the [ruby version manager](http://rvm.io) is installed.

* Install ruby version 2.1.x with command `rvm install 2.1`.

* Install the bundler gem with `gem install bundler`.

* Install the gem bundle with `bundle install`.

* Download [dovecot 2.2.15](http://dovecot.org/releases/2.2/dovecot-2.2.15.tar.gz) and extract
  the source to dovecot/source

* Configure the source with the command `bundle exec rake dovecot:source:configure`.

* Install dovecot into dovecot/target with `bundle exec rake dovecot:install`.

* Type `make all` to compile the plugin.

* Find the plugin at dovecot/target/lib/dovecot/lib18_scrambler_plugin.so.

Tests
-----

All tests are written with RSpec and can be run with `make spec-all` or `bundle exec rake spec:integration`

Since dovecot needs root privileges to run, you need create the file `sudo.password` with contains the
password that is needed for running dovecot with sudo.

Project
-------

Concept, design and realization by [Posteo e.K.](https://posteo.de).
The implementation was provided by [simia.tech GbR](http://simiatech.com).
An security audit has been provided by [Cure53](https://cure53.de).
