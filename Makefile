default: install

CFLAGS := -Wall -Wextra -Werror -Wshadow -Wno-type-limits -g3 -O0 -Wpointer-arith -fvisibility=hidden

prefix:=$(shell pkg-config --variable=prefix enlightenment)
release=$(shell pkg-config --variable=release enlightenment)
host_cpu=$(shell uname -m)
MODULE_ARCH="linux-gnu-$(host_cpu)-$(release)"

src/e_mod_main.o: src/e_mod_main.c
	gcc -fPIC -g -c src/e_mod_main.c $(CFLAGS) `pkg-config --cflags enlightenment elementary eeze` -o src/e_mod_main.o

src/module.so: src/e_mod_main.o
	gcc -shared -fPIC -DPIC src/e_mod_main.o `pkg-config --libs enlightenment elementary eeze` -Wl,-soname -Wl,module.so -o src/module.so

install: src/module.so src/zync
	sudo mkdir -p $(prefix)'/lib/enlightenment/modules/zync/'$(MODULE_ARCH)
	sudo install -c src/module.so $(prefix)/lib/enlightenment/modules/zync/$(MODULE_ARCH)/module.so
	sudo install -c module.desktop $(prefix)/lib/enlightenment/modules/zync/module.desktop
	sudo install -c -m 644 images/*.png $(prefix)/lib/enlightenment/modules/zync/
	sudo install -c -m 755 src/zync /$(prefix)/bin/

clean:
	rm -rf src/module.so src/e_mod_main.o
