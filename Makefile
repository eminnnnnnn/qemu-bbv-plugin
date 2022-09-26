QEMU_INCLUDEDIR=$(QEMU_ROOT)/include
QEMU_BIN=$(QEMU_ROOT)/bin/qemu-x86_64
CXX_FLAGS=-Wall
LIBS_CXX_FLAGS=$(CXX_FLAGS) -std=c++17 -fPIC -shared -I $(QEMU_INCLUDEDIR)
BBV_SRCS=bbv_caches.cc bbv.cc cache.cc common.cc
SIMRUN_SRCS=

libs: libbbv_caches.so libsimpoint_run.so
test: mtx_mul
all: libs test

# lib%.so: %.cc
#	g++ -Wl,-soname,$@ $(LIBS_CXX_FLAGS) $^ -o $@

libbbv_caches.so: $(BBV_SRCS)
	g++ -Wl,-soname,$@ $(LIBS_CXX_FLAGS) $^ -o $@

librun_simpoint.so: $(SIMRUN_SRCS)
	g++ -Wl,-soname,$@ $(LIBS_CXX_FLAGS) $^ -o $@

mtx_mul: mtx_mul.cc
	g++ $(CXX_FLAGS) $^ -o $@

collect_stats: libs test
	$(QEMU_BIN) -d plugin -plugin ./libbbv_caches.so,outfile=test.bbv,interval=100000 ./mtx_mul

run_simpoint: libs test collect_stats
	$(QEMU_BIN) -d plugin -plugin ./librun_simpoint.so ./mtx_mul

clean:
	rm -f *.so mtx_mul

.PHONY: all libs test collect_stats run_simpoint clean
