#ifndef __CONFIG__
#define __CONFIG__

#define ROTARY2 (32)
#define BUTTON (34)
#define ROTARY1 (35)

#define DISPLAY_I2C (I2C_NUM_0)
#define DISPLAY_I2C_SCL (25)
#define DISPLAY_I2C_SDA (26)
#define DISPLAY_I2C_FREQ (400000)
#define DISPLAY_ADR 0x3C  // 011110+SA0+RW - 0x3C or 0x3D

#endif
