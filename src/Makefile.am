include $(top_srcdir)/common.am

bin_PROGRAMS = hello_world

hello_world_SOURCES = hello_world.c
hello_world_CFLAGS = $(AM_CFLAGS) $(MODBUS_CFLAGS) $(BACNET_CFLAGS)
hello_world_LDADD = $(AM_LIBS) $(MODBUS_LIBS) $(BACNET_LIBS)

BUILT_SOURCES = bacnet_namespace.h .bacnet_dependent_src_stamp

EXTRA_DIST = bacnet_api_names

.bacnet_dependent_src_stamp: bacnet_api_names $(BACNET_DEPENDENT_SRC)
	cat $< | $(top_srcdir)/scripts/check_namespace.sh $< $?
	touch $@
bacnet_namespace.h: bacnet_api_names
	cat $< | $(top_srcdir)/scripts/build_namespace.sh > $@
