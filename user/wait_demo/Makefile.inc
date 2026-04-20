# -*-Makefile-*-

OBJDIRS		+= $(USER_OBJDIR)/wait_demo

USER_WAITDEMO_SRC	:= $(USER_DIR)/wait_demo/wait_demo.c
USER_WAITDEMO_OBJ	:= $(patsubst %.c, $(OBJDIR)/%.o, $(USER_WAITDEMO_SRC))
KERN_BINFILES	+= $(USER_OBJDIR)/wait_demo/wait_demo

wait_demo: $(USER_OBJDIR)/wait_demo/wait_demo

$(USER_OBJDIR)/wait_demo/wait_demo: $(USER_LIB_OBJ) $(USER_WAITDEMO_OBJ)
	@echo + ld[USER/wait_demo] $@
	$(V)$(LD) $(USER_LDFLAGS) -o $@ $(USER_LIB_OBJ) $(USER_WAITDEMO_OBJ) $(GCC_LIBS)
	$(V)$(OBJDUMP) -S $@ > $@.asm
	$(V)$(NM) -n $@ > $@.sym

$(USER_OBJDIR)/wait_demo/%.o: $(USER_DIR)/wait_demo/%.c
	@echo + cc[USER/wait_demo] $<
	@mkdir -p $(@D)
	$(V)$(CC) $(USER_CFLAGS) -c -o $@ $<
