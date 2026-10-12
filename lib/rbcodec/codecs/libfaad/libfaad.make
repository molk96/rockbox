#             __________               __   ___.
#   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
#   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
#   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
#   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
#                     \/            \/     \/    \/            \/
#

# libfaad
FAADLIB := $(CODECDIR)/libfaad.a
FAADLIB_SRC := $(call preprocess, $(RBCODECLIB_DIR)/codecs/libfaad/SOURCES)
FAADLIB_OBJ := $(call c2obj, $(FAADLIB_SRC))
OTHER_SRC += $(FAADLIB_SRC)
OTHER_INC += -I$(RBCODECLIB_DIR)/codecs/libfaad

$(FAADLIB): $(FAADLIB_OBJ)
	$(SILENT)$(shell rm -f $@)
	$(call PRINTS,AR $(@F))$(AR) rcs $@ $^ >/dev/null

# glue for the "ambient" audio layer in the main firmware (apps/ambient.c).
# libfaad.a and libcodec.a are linked into the firmware together with it.
AMBIENTAACLIB := $(CODECDIR)/libfaad-ambient.a
AMBIENTAACLIB_OBJ := $(CODECDIR)/libfaad-ambient/ambient_aac.o

$(AMBIENTAACLIB): $(AMBIENTAACLIB_OBJ)
	$(SILENT)$(shell rm -f $@)
	$(call PRINTS,AR $(@F))$(AR) rcs $@ $^ >/dev/null

$(CODECDIR)/libfaad-ambient/%.o: $(RBCODECLIB_DIR)/codecs/libfaad/%.c
	$(SILENT)mkdir -p $(dir $@)
	$(call PRINTS,CC $(subst $(ROOTDIR)/,,$<))$(CC) \
		-I$(dir $<) $(CODECFLAGS) -O2 -c $< -o $@
