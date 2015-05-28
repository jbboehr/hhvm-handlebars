#!/bin/sh

# set -e

export DEBIAN_FRONTEND=noninteractive

case "$1" in
before_install)
    sudo rm -f /etc/apt/sources.list.d/*rabbit*
    sudo apt-get update -qq
    sudo apt-get install -y -q software-properties-common python-software-properties
    sudo add-apt-repository -y ppa:ubuntu-toolchain-r/test
    sudo apt-add-repository -y ppa:jbboehr/ppa
    sudo apt-add-repository -y ppa:jbboehr/handlebars
    sudo apt-add-repository -y ppa:mandel/movie-tracker
    sudo apt-get update -qq
    sudo apt-get install -y -q libhandlebars-dev libtalloc-dev g++-4.8 gcc-4.8 libboost1.49-dev libgoogle-glog-dev libjemalloc-dev
    sudo apt-get install -y -q -o Dpkg::Options::="--force-confdef" -o Dpkg::Options::="--force-confnew" hhvm hhvm-dev
    sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-4.8 90
    ;;
install)
    hphpize
    cmake .
    make
    sudo make install
    echo "hhvm.dynamic_extension_path=`pwd`" | sudo tee -a /etc/hhvm/php.ini
    echo "hhvm.dynamic_extensions[handlebars]=handlebars.so" | sudo tee -a /etc/hhvm/php.ini
    ;;
before_script)
    php generate-tests.php
    ;;
esac

