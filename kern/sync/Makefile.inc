# -*-Makefile-*-

OBJDIRS		+= $(KERN_OBJDIR)/sync

KERN_SRCFILES	+= $(KERN_DIR)/sync/waitset.c

$(KERN_OBJDIR)/sync/%.o: $(KERN_DIR)/sync/%.c
	@echo + $(COMP_NAME)[KERN/sync] $<
	@mkdir -p $(@D)
	$(V)$(CCOMP) $(CCOMP_KERN_CFLAGS) -c -o $@ $<
