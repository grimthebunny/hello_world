include $(top_srcdir)/common.am

bin_PROGRAMS = hello_world

hello_world_SOURCES = hello_world.c
hello_world_CFLAGS = $(MODBUS_CFLAGS) $(BACNET_CFLAGS)
hello_world_LDADD = $(MODBUS_LIBS) $(BACNET_LIBS)
