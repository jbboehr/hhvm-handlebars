
# hhvm-handlebars

[![Build Status](https://travis-ci.org/jbboehr/hhvm-handlebars.svg?branch=master)](https://travis-ci.org/jbboehr/hhvm-handlebars)

HHVM bindings for [handlebars.c](https://github.com/jbboehr/handlebars.c). Use with [handlebars.php](https://github.com/jbboehr/handlebars.php).


## Installation

### Source

Install [handlebars.c](https://github.com/jbboehr/handlebars.c)

#### Ubuntu

Note: For precise, see [.travis.yml](https://github.com/jbboehr/hhvm-handlebars/blob/master/.travis.yml)

```bash
# HHVM dev dependencies
sudo apt-get install libboost-dev libjemalloc-dev libgoogle-glog-dev libtbb-dev

git clone https://github.com/jbboehr/hhvm-handlebars.git --recursive
cd hhvm-handlebars
hphpize
cmake .
make
sudo make install
```

To enable, add this to your HHVM config:
```
hhvm.dynamic_extension_path = /path/to/hhvm/extensions
hhvm.dynamic_extensions[handlebars] = handlebars.so
```

## License

This project is licensed under the [LGPLv3](http://www.gnu.org/licenses/lgpl-3.0.txt).
handlebars.js is licensed under the [MIT license](http://opensource.org/licenses/MIT).

