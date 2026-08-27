SRCDIR ?= /opt/fpp/src
include $(SRCDIR)/makefiles/common/setup.mk
include $(SRCDIR)/makefiles/platform/*.mk

all: libfpp-artnet-prop-control-secondary.$(SHLIB_EXT)
debug: all

OBJECTS_artnet_prop_control_secondary_so += src/ArtNetPropControlSecondary.o
LIBS_artnet_prop_control_secondary_so += -L$(SRCDIR) -lfpp
CXXFLAGS_src/ArtNetPropControlSecondary.o += -I$(SRCDIR)

%.o: %.cpp Makefile
	$(CCACHE) $(CC) $(CFLAGS) $(CXXFLAGS) $(CXXFLAGS_$@) -c $< -o $@

libfpp-artnet-prop-control-secondary.$(SHLIB_EXT): $(OBJECTS_artnet_prop_control_secondary_so) $(SRCDIR)/libfpp.$(SHLIB_EXT)
	$(CCACHE) $(CC) -shared $(CFLAGS_$@) $(OBJECTS_artnet_prop_control_secondary_so) $(LIBS_artnet_prop_control_secondary_so) $(LDFLAGS) -o $@

clean:
	rm -f libfpp-artnet-prop-control-secondary.so libfpp-artnet-prop-control-secondary.dylib $(OBJECTS_artnet_prop_control_secondary_so)
