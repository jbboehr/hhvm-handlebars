#!/bin/sh

set -e

export DEBIAN_FRONTEND=noninteractive
export PREFIX=$HOME/build
export PATH="$PREFIX/bin:$PATH"
export CFLAGS="-L$PREFIX/lib"
export CPPFLAGS="-I$PREFIX/include"
export LDFLAGS="-L$PREFIX/lib"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig"

case "$1" in
apt)
	sudo rm -f /etc/apt/sources.list.d/*rabbit*
	sudo apt-get update -qq
	sudo apt-get install -y -q software-properties-common python-software-properties
	sudo add-apt-repository -y ppa:ubuntu-toolchain-r/test
	sudo add-apt-repository -y ppa:boost-latest/ppa
	sudo apt-get update -qq
	sudo apt-get install -y -q libtalloc-dev g++-4.8 gcc-4.8 libboost1.55-all-dev libjson0-dev libpcre3-dev libtalloc-dev pkg-config
	sudo apt-get install -y -q -o Dpkg::Options::="--force-confdef" -o Dpkg::Options::="--force-confnew" hhvm hhvm-dev
	sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-4.8 90
	;;

install_check)
	if [ ! -f $PREFIX/include/check.h ]; then
		wget http://downloads.sourceforge.net/project/check/check/0.9.14/check-0.9.14.tar.gz
		tar xfv check-0.9.14.tar.gz
		cd check-0.9.14
		./configure --prefix=$PREFIX
		make
		make install
		cd ..
		rm -Rf check-0.9.14.tar.gz check-0.9.14
	fi
	;;

install_bison)
	if [ ! -f $PREFIX/bin/bison ]; then
		wget http://gnu.mirror.iweb.com/bison/bison-3.0.2.tar.gz
		tar xfv bison-3.0.2.tar.gz
		cd bison-3.0.2
		./configure --prefix=$PREFIX
		make
		make install
		cd ..
		rm -Rf bison-3.0.2 bison-3.0.2.tar.gz
	fi
	;;

install_handlebars)
	if [ ! -f $PREFIX/include/handlebars.h ]; then
		git clone -b v$LIBHANDLEBARS_VERSION https://github.com/jbboehr/handlebars.c handlebars-c --recursive
		cd handlebars-c
		./bootstrap
		./configure --prefix=$PREFIX
		make install
		cd ..
		rm -Rf handlebars-c
	fi
	;;

install_glog)
	if [ ! -f $PREFIX/include/glog/logging.h ]; then
		svn checkout http://google-glog.googlecode.com/svn/trunk/ google-glog
		cd google-glog
		./configure --prefix=$PREFIX
		make
		make install
		cd ..
		rm -Rf google-glog
	fi
	;;

install_jemalloc)
	if [ ! -f $PREFIX/include/jemalloc/jemalloc.h ]; then
		wget http://www.canonware.com/download/jemalloc/jemalloc-3.6.0.tar.bz2
		tar xjvf jemalloc-3.6.0.tar.bz2
		cd jemalloc-3.6.0
		./configure --prefix=$PREFIX
		make
		make install
		cd ..
		rm -Rf jemalloc-3.6.0.tar.bz2 jemalloc-3.6.0
	fi
	;;

install_hhvm_handlebars)
	hphpize
	cmake -DHANDLEBARS_INCLUDE_DIR=$PREFIX/include -DHANDLEBARS_LIBRARY=$PREFIX/lib/libhandlebars.so .
	make
	sudo make install
	echo "hhvm.dynamic_extensions[handlebars]=`pwd`/handlebars.so" | sudo tee -a /etc/hhvm/php.ini
	;;

before_script)
	php generate-tests.php
	;;

script)
	./test
	;;

esac


