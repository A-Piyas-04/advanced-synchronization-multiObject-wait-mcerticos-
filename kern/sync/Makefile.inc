# -*-Makefile-*-

OBJDIRS += $(KERN_OBJDIR)/sync

KERN_SRCFILES += $(KERN_DIR)/sync/waitobj.c

$(KERN_OBJDIR)/sync/%.o: $(KERN_DIR)/sync/%.c
	@echo + cc[KERN/sync] $<
	@mkdir -p $(@D)
	$(V)$(CC) $(KERN_CFLAGS) -c -o $@ $<

