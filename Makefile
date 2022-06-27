#!/usr/bin/make -f

CPPFLAGS := $(CPPFLAGS) -D_FILE_OFFSET_BITS=64 -MMD
LDLIBS := -lbsd

SOURCES := ls-l.c unique.c readdir.c
OBJECTS := $(SOURCES:%.c=%.o)
DEPS := $(OBJECTS:%.o=%.d)

ls-l: $(OBJECTS)

clean:
	$(RM) ls-l $(OBJECTS) $(DEPS)

-include $(DEPS)
