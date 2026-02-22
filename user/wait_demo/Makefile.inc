# -*-Makefile-*-

OBJDIRS += $(USER_OBJDIR)/wait_demo

USER_WAIT_DEMO_SRC += $(USER_DIR)/wait_demo/wait_demo.c
USER_WAIT_DEMO_OBJ := $(patsubst %.c, $(OBJDIR)/%.o, $(USER_WAIT_DEMO_SRC))
USER_WAIT_DEMO_OBJ := $(patsubst %.S, $(OBJDIR)/%.o, $(USER_WAIT_DEMO_OBJ))
KERN_BINFILES += $(USER_OBJDIR)/wait_demo/wait_demo

wait_demo: $(USER_OBJDIR)/wait_demo/wait_demo

$(USER_OBJDIR)/wait_demo/wait_demo: $(USER_LIB_OBJ) $(USER_WAIT_DEMO_OBJ)
	@echo + ld[USER/wait_demo] $@
	$(V)$(LD) -o $@ $(USER_LDFLAGS) $(USER_LIB_OBJ) $(USER_WAIT_DEMO_OBJ) $(GCC_LIBS)
	$(V)$(OBJDUMP) -S $@ > $@.asm
	$(V)$(NM) -n $@ > $@.sym

$(USER_OBJDIR)/wait_demo/%.o: $(USER_DIR)/wait_demo/%.c
	@echo + cc[USER/wait_demo] $<
	@mkdir -p $(@D)
	$(V)$(CC) $(USER_CFLAGS) -c -o $@ $<

$(USER_OBJDIR)/wait_demo/%.o: $(USER_DIR)/wait_demo/%.S
	@echo + as[USER/wait_demo] $<
	@mkdir -p $(@D)
	$(V)$(CC) $(USER_CFLAGS) -c -o $@ $<

