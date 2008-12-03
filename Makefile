TARGET := image
TOPDIR = .

SRC = \
	main.c            \
	eeprom_reserve.c  \
	pixel.c           \
	util.c            \


LAUNCH_BOOTLOADER = launch-bootloader
SERIAL = /dev/ttyUSB0	

export TOPDIR
##############################################################################
all: compile-$(TARGET)
	@echo "==============================="
	@echo "$(TARGET) compiled for: $(MCU)"
	@echo "size is: "
	@${TOPDIR}/scripts/size $(TARGET)
	@echo "==============================="


##############################################################################
# generic fluff
include defaults.mk
#include $(TOPDIR)/rules.mk

##############################################################################
# generate SUBDIRS variable
#

.subdirs: autoconf.h
	$(RM) -f $@
	echo "SUBDIRS += borg_hw" >> $@
	echo "SUBDIRS += animations" >> $@
	(for subdir in `grep -e "^#define .*_SUPPORT" autoconf.h \
	      | sed -e "s/^#define //" -e "s/_SUPPORT.*//" \
	      | tr "[A-Z]\\n" "[a-z] " `; do \
	  test -d $$subdir && echo "SUBDIRS += $$subdir" ; \
	done) | sort -u >> $@

ifneq ($(no_deps),t)
ifneq ($(MAKECMDGOALS),clean)
ifneq ($(MAKECMDGOALS),mrproper)
ifneq ($(MAKECMDGOALS),menuconfig)

include $(TOPDIR)/.subdirs
include $(TOPDIR)/.config

endif # MAKECMDGOALS!=menuconfig
endif # MAKECMDGOALS!=mrproper
endif # MAKECMDGOALS!=clean
endif # no_deps!=t

##############################################################################

.PHONY: compile-subdirs
compile-subdirs:
	for dir in $(SUBDIRS); do make -C $$dir lib$$dir.a || exit 5; done

.PHONY: compile-$(TARGET)
compile-$(TARGET): compile-subdirs $(TARGET).hex $(TARGET).bin $(TARGET).lst

OBJECTS += $(patsubst %.c,%.o,${SRC})
LINKLIBS = $(foreach subdir,$(SUBDIRS),$(subdir)/lib$(subdir).a)

# FIXME how can we omit specifying every file to be linked twice?
# This is currently necessary because of interdependencies between
# the libraries, which aren't denoted in these however.
$(TARGET): $(OBJECTS) $(LINKLIBS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) \
	  $(foreach subdir,$(SUBDIRS),-L$(subdir) -l$(subdir)) \
	  $(foreach subdir,$(SUBDIRS),-l$(subdir)) \
	  $(foreach subdir,$(SUBDIRS),-l$(subdir))


##############################################################################

%.hex: %
	$(OBJCOPY) -O ihex -R .eeprom $< $@

ifeq ($(HTTPD_INLINE_FILES_SUPPORT),y)
INLINE_FILES := $(wildcard httpd/embed/*)
else
INLINE_FILES :=
endif

%.bin: % $(INLINE_FILES)
	$(OBJCOPY) -O binary -R .eeprom $< $@
ifeq ($(HTTPD_INLINE_FILES_SUPPORT),y)
	$(MAKE) -C httpd httpd-concat
	httpd/do-embed $(INLINE_FILES)
	$(OBJCOPY) -O ihex -I binary $(TARGET).bin $(TARGET).hex
endif

%.eep.hex: %
	$(OBJCOPY) --set-section-flags=.eeprom="alloc,load" --change-section-lma .eeprom=0 -O ihex -j .eeprom $< $@

%.lst: %
	$(OBJDUMP) -h -S $< > $@

%-size: %.hex
	$(SIZE) $<

##############################################################################
CONFIG_SHELL := $(shell if [ -x "$$BASH" ]; then echo $$BASH; \
          else if [ -x /bin/bash ]; then echo /bin/bash; \
          else echo sh; fi ; fi)

menuconfig:
	$(MAKE) -C scripts/lxdialog all
	$(CONFIG_SHELL) scripts/Menuconfig config.in
	test -e .config
	@$(MAKE) no_deps=t what-now-msg

what-now-msg:
	@echo ""
	@echo "Next, you can: "
	@echo " * 'make' to compile your borgware"
	@for subdir in $(SUBDIRS); do \
	  test -e "$$subdir/configure" -a -e "$$subdir/cfgpp" \
	    && echo " * 'make $$subdir/menuconfig' to" \
	            "further configure $$subdir"; done || true
	@echo ""
.PHONY: what-now-msg

%/menuconfig:
	$(SH) "$(@D)/configure"
	@$(MAKE) what-now-msg

##############################################################################
clean:
	$(MAKE) -f rules.mk no_deps=t clean-common
	$(RM) $(TARGET) $(TARGET).bin $(TARGET).hex .subdirs
	for subdir in `find . -type d`; do \
	  test "x$$subdir" != "x." \
	  && test -e $$subdir/Makefile \
	  && make no_deps=t -C $$subdir clean; done

mrproper:
	$(MAKE) clean
	$(RM) -f autoconf.h .config config.mk .menuconfig.log .config.old

sflash: $(TARGET).hex
	$(LAUNCH_BOOTLOADER) $(SERIAL) 115200
	avrdude -p m32 -b 115200 -u -c avr109 -P $(SERIAL) -U f:w:$< -F
	echo X > $(SERIAL)


.PHONY: clean mrproper sflash
##############################################################################
# configure ethersex
#
show-config: autoconf.h
	@echo
	@echo "These modules are currently enabled: "
	@echo "======================================"
	@grep -e "^#define .*_SUPPORT" autoconf.h | sed -e "s/^#define / * /" -e "s/_SUPPORT.*//"

.PHONY: show-config

autoconf.h .config: 
	@echo make\'s goal: $(MAKECMDGOALS)
ifneq ($(MAKECMDGOALS),menuconfig)
	# make sure menuconfig isn't called twice, on `make menuconfig'
	test -s autoconf.h -a -s .config || $(MAKE) no_deps=t menuconfig
	# test the target file, test fails if it doesn't exist
	# and will keep make from looping menuconfig.
	test -s autoconf.h -a -s .config
endif

include depend.mk
