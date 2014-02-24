SHELL      = /bin/sh
CC         = /usr/bin/gcc
VER        = $(shell cat VERSION)

mdir = $(filter-out $(wildcard $(1)), $(1))

.PHONY: all testing debug install uninstall clean

all: bin doc

testing: bin

debug: bin

install: install-bin install-doc

uninstall: uninstall-bin uninstall-doc uninstall-service

clean: clean-bin clean-doc

############################
# Building the Application #
############################
CFLAGS    = -Wall -D 'VERSION="$(VER)"'

ifneq (,$(findstring debug, $(MAKECMDGOALS)))
CFLAGS   += -g
else
CFLAGS   += -O2
endif

	
LIBS = -lxcb
_OBJ = bouncer.o
OBJ  = $(addprefix obj/, $(_OBJ))

BIN_PATH  = $(prefix)/usr/
BINARIES  = bin/bouncer bin/bouncer-global
BIN_INST  = $(BINARIES:%=$(BIN_PATH)%)


.PHONY: bin install-bin uninstall-bin clean-bin

bin: $(BINARIES)

install-bin: $(BIN_INST)

uninstall-bin:
	@echo "==> Removing binaries..."
	-rm -f $(BIN_INST)

clean-bin:
	@echo "==> Cleaning binaries..."
	-rm -rf obj/
	-rm -f bin/bouncer
	-chmod -x bin/bouncer-global

# Link
bin/bouncer: $(OBJ) $(call mdir, bin/)
	@echo "==> Building $@..."
	$(CC) $(LIBS) $(filter-out bin/, $^) -o $@

# Compile sources, create dependency files
obj/%.o: src/%.c $(call mdir, obj/) VERSION
	@echo "==> Compiling $@..."
	$(CC) $(CFLAGS) -c $< -o $@
	$(CC) $(CFLAGS) -M -MP $< | sed -e "1 s/^/obj\//" > obj/$*.P
	echo -e "\n$<:" >> obj/$*.P

# Include dependency files
-include $(wildcard obj/*.P)


################
# other files #
################
SERVICE_INST = /usr/lib/systemd/system/bouncer@.service

.PHONY: install-service uninstall-service

install-service: $(SERVICE_INST)

uninstall-service:
	@echo "==> Removing service file..."
	-rm -f $(SERVICE_INST)

######################
# Generate man-pages #
######################
DOC_TMP  = $(basename $(wildcard doc/*.source))
DOC_COMP  = $(DOC_TMP:%=%.gz)

MAN_PATH  = $(prefix)/usr/share/man/man
DOC_INST  = $(foreach tmp, $(notdir $(DOC_TMP)), $(MAN_PATH)$(subst .,,$(suffix $(tmp)))/$(tmp).gz)


.PHONY: doc install-doc uninstall-doc clean-doc

doc: $(DOC_COMP)

install-doc: $(DOC_INST)

uninstall-doc:
	@echo "==> Removing man pages..."
	-rm -f $(DOC_INST)

clean-doc:
	@echo "==> Cleaning docs..."
	-rm -f $(DOC_COMP) $(DOC_TMP)

# Insert Version
$(DOC_TMP): %: %.source VERSION
	@echo "==> Inserting version to $@..."
	sed -e "s/%%VERSION%%/$(VER)/" $< > $@

# Compress man page
$(DOC_COMP): %.gz: %
	@echo "==> Compressing $@..."
	gzip -c $< > $@

##################
# Create folders #
##################

.PRECIOUS: %/
%/:
	mkdir $@


###############################
# Second Expansion targets #
###############################

# Installing man page
.SECONDEXPANSION:
$(DOC_INST): %: doc/$$(notdir %) $$(dir %)
	@echo "==> Installing $@..."
	cp $< $@

INST = $(BIN_INST) $(SERVICE_INST)
# Installing general
VPATH = bin
$(INST): %: $$(notdir %) 	
	@echo "==> Installing $@..."
	cp $^ $@
