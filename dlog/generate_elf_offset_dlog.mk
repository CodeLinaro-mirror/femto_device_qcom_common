# Generates elf_offset_dlog.bin by scanning all vendor ELFs for dlog sections.
# Must be included from AndroidBoard.mk (after all Android.mk files are processed)
# so that ALL_DEFAULT_INSTALLED_MODULES is fully populated.

ifeq ($(TARGET_USES_DLOG),true)

DLOG_GENERATE_SCRIPT := device/qcom/common/dlog/generate_dlog.sh
INSTALLED_ELF_OFFSET_DLOG := $(TARGET_OUT_VENDOR)/bin/elf_offset_dlog.bin

# All vendor installed modules EXCEPT our own output (breaks circular dep).
# Safe to use := here because AndroidBoard.mk runs after all Android.mk files.
DLOG_VENDOR_ELF_DEPS := $(filter-out $(INSTALLED_ELF_OFFSET_DLOG), \
    $(filter $(TARGET_OUT_VENDOR)/%,$(ALL_DEFAULT_INSTALLED_MODULES)))

DLOG_READELF := $(if $(LLVM_PREBUILTS_PATH),$(LLVM_PREBUILTS_PATH)/llvm-readelf,readelf)
DLOG_OBJCOPY := $(if $(LLVM_PREBUILTS_PATH),$(LLVM_PREBUILTS_PATH)/llvm-objcopy,objcopy)

$(INSTALLED_ELF_OFFSET_DLOG): $(DLOG_GENERATE_SCRIPT) $(DLOG_VENDOR_ELF_DEPS)
	@echo "------ Generating elf_offset_dlog.bin ------"
	DLOG_READELF=$(DLOG_READELF) \
	DLOG_OBJCOPY=$(DLOG_OBJCOPY) \
	bash $(DLOG_GENERATE_SCRIPT) $(TARGET_OUT_VENDOR)/bin

# Register so vendorimage file_list.txt includes it and the image picks it up
ALL_DEFAULT_INSTALLED_MODULES += $(INSTALLED_ELF_OFFSET_DLOG)

# Explicit ordering: vendorimage must wait for our .bin
$(INSTALLED_VENDORIMAGE_TARGET): $(INSTALLED_ELF_OFFSET_DLOG)

endif # TARGET_USES_DLOG
