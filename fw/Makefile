TARGET = mtb-usb-4
DEBUG = 1
OPT = -Os
BUILD_DIR = build

STM32_SRC_PATH = ${STM32_CUBE_PATH}/Drivers/STM32F1xx_HAL_Driver/Src
STM32_DRIVERS_PATH = ${STM32_CUBE_PATH}/Drivers
# Something like '~/STM32Cube/Repository/STM32Cube_FW_F1_V1.8.3/Drivers/STM32F1xx_HAL_Driver/Src/'

CPP_SOURCES = $(wildcard src/*.cpp)

C_SOURCES = \
	$(STM32_SRC_PATH)/stm32f1xx_hal_gpio_ex.c \
	$(STM32_SRC_PATH)/stm32f1xx_hal_i2c.c \
	$(STM32_SRC_PATH)/stm32f1xx_hal.c \
	$(STM32_SRC_PATH)/stm32f1xx_hal_rcc.c \
	$(STM32_SRC_PATH)/stm32f1xx_hal_rcc_ex.c \
	$(STM32_SRC_PATH)/stm32f1xx_hal_gpio.c \
	$(STM32_SRC_PATH)/stm32f1xx_hal_dma.c \
	$(STM32_SRC_PATH)/stm32f1xx_hal_cortex.c \
	$(STM32_SRC_PATH)/stm32f1xx_hal_pwr.c \
	$(STM32_SRC_PATH)/stm32f1xx_hal_flash.c \
	$(STM32_SRC_PATH)/stm32f1xx_hal_flash_ex.c \
	$(STM32_SRC_PATH)/stm32f1xx_hal_exti.c \
	$(STM32_SRC_PATH)/stm32f1xx_hal_tim.c \
	$(STM32_SRC_PATH)/stm32f1xx_hal_tim_ex.c \
	$(STM32_SRC_PATH)/stm32f1xx_hal_uart.c \
	$(STM32_SRC_PATH)/stm32f1xx_hal_pcd.c \
	$(STM32_SRC_PATH)/stm32f1xx_hal_pcd_ex.c \
	$(STM32_SRC_PATH)/stm32f1xx_hal_iwdg.c \
	lib/libusb_stm32/src/usbd_stm32f103_devfs.c \
	lib/libusb_stm32/src/usbd_core.c \
	$(wildcard src/*.c)

ASM_SOURCES = startup_stm32f103xb.s

PREFIX = arm-none-eabi-

# The gcc compiler bin path can be either defined in make command via GCC_PATH variable (> make GCC_PATH=xxx)
# either it can be added to the PATH environment variable.
ifdef GCC_PATH
CC = $(GCC_PATH)/$(PREFIX)gcc
CPP = $(GCC_PATH)/$(PREFIX)g++
LD = $(GCC_PATH)/$(PREFIX)gcc
AS = $(GCC_PATH)/$(PREFIX)g++ -x assembler-with-cpp
CP = $(GCC_PATH)/$(PREFIX)objcopy
SZ = $(GCC_PATH)/$(PREFIX)size
else
CC = $(PREFIX)gcc
CPP = $(PREFIX)g++
LD = $(PREFIX)gcc
AS = $(PREFIX)g++ -x assembler-with-cpp
CP = $(PREFIX)objcopy
SZ = $(PREFIX)size
endif
HEX = $(CP) -O ihex
BIN = $(CP) -O binary -S

CPU = -mcpu=cortex-m3
MCU = $(CPU) -mthumb $(FPU) $(FLOAT-ABI)


AS_DEFS =

C_DEFS = \
	-DUSE_HAL_DRIVER \
	-DSTM32F103xB

CPP_DEFS = \
	-DUSE_HAL_DRIVER \
	-DSTM32F103xB


CPP_INCLUDES =

C_INCLUDES =

C_OR_CPP_INCLUDES = \
	-I$(STM32_DRIVERS_PATH)/STM32F1xx_HAL_Driver/Inc \
	-I$(STM32_DRIVERS_PATH)/STM32F1xx_HAL_Driver/Inc/Legacy \
	-I$(STM32_DRIVERS_PATH)/CMSIS/Device/ST/STM32F1xx/Include \
	-I$(STM32_DRIVERS_PATH)/CMSIS/Include \
	-Ilib/libusb_stm32/inc \
	-Ilib/eeprom \
	-Ilib/ina219 \
	-Ilib/dwt_delay \
	-I inc

AS_INCLUDES =


ASFLAGS = $(MCU) $(AS_DEFS) $(AS_INCLUDES) $(OPT) -Wall -fdata-sections -ffunction-sections
CFLAGS = $(MCU) $(C_DEFS) $(C_INCLUDES) $(C_OR_CPP_INCLUDES) $(OPT) -Wall -fdata-sections -ffunction-sections
CPPFLAGS = $(MCU) $(CPP_DEFS) $(CPP_INCLUDES) $(C_OR_CPP_INCLUDES) $(OPT) -Wall -fdata-sections -ffunction-sections -std=c++17

ifeq ($(DEBUG), 1)
CFLAGS += -g -gdwarf-2
CPPFLAGS += -g -gdwarf-2
endif

CFLAGS += -MMD -MP -MF"$(@:%.o=%.d)"
CPPFLAGS += -MMD -MP -MF"$(@:%.o=%.d)"

CFLAGS += \
	-DUSBD_DP_PORT=GPIOA \
	-DUSBD_DP_PIN=10

CPPFLAGS += \
	-DUSBD_DP_PORT=GPIOA \
	-DUSBD_DP_PIN=10


LDSCRIPT = STM32F103C8Tx_FLASH.ld

LIBS = -lc -lm -lnosys
LIBDIR =
LDFLAGS = $(MCU) -specs=nosys.specs $(LIBDIR) $(LIBS) -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections -T$(LDSCRIPT)

all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin


OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))

OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(CPP_SOURCES:.cpp=.o)))
vpath %.cpp $(sort $(dir $(CPP_SOURCES)))

OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.c=.lst)) $< -o $@

$(BUILD_DIR)/%.o: %.cpp Makefile | $(BUILD_DIR)
	$(CPP) -c $(CPPFLAGS) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.cpp=.lst)) $< -o $@

$(BUILD_DIR)/%.o: %.s Makefile | $(BUILD_DIR)
	$(AS) -c $(ASFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile
	$(LD) $(OBJECTS) $(LDFLAGS) -o $@
	$(SZ) $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(HEX) $< $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(BIN) $< $@

$(BUILD_DIR):
	mkdir $@

clean:
	-rm -fR $(BUILD_DIR)

flash_stlink:
	st-flash --reset write $(BUILD_DIR)/$(TARGET).bin 0x08001000

-include $(wildcard $(BUILD_DIR)/*.d)

.PHONY: clean flash_stlink
