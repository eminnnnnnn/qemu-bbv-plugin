QEMU_INCLUDEDIR="./qemu/include/"
QEMU_BIN="./qemu-aarch64"
CXX_FLAGS=-Wall -Werror
LIBS_CXX_FLAGS=$(CXX_FLAGS) -fPIC -shared -I $(QEMU_INCLUDEDIR)

libs: libbbv_caches.so libsimpoint_run.so
test: mtx_mul
all: libs test

lib%.so: %.cc
	g++ -Wl,-soname,$@ $(LIBS_CXX_FLAGS) $^ -o $@

mtx_mul: main.cc
	g++ $(CXX_FLAGS) $^ -o $@

collect_stats: libs test
	$(QEMU_BIN) -d plugin -plugin,libbbv_caches.so ./mtx_mul

run_simpoint: libs test collect_stats
	$(QEMU_BIN) -d plugin -plugin,librun_simpoint.so ./mtx_mul

clean:
	rm -f *.so mtx_mul

.PHONY: all libs test collect_stats run_simpoint clean
