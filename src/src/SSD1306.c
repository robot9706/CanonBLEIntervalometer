#include "SSD1306.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_err.h"
#include "ascii_font.h"

static i2c_port_t i2c_port;

#define ACK_CHECK_EN 0x1
#define ACK_CHECK_DIS 0x0

static int8_t _i2caddr;
static uint8_t _vccstate;
static uint8_t buffer[SSD1306_LCDHEIGHT * SSD1306_LCDWIDTH / 8] = { 0 };

void SSD1306_begin(uint8_t vccstate, uint8_t i2caddr, i2c_port_t i2c)
{
	_vccstate = vccstate;
	_i2caddr = i2caddr;

	i2c_port = i2c;

	// Init sequence
	SSD1306_command(SSD1306_DISPLAYOFF);                    // 0xAE
	SSD1306_command(SSD1306_SETDISPLAYCLOCKDIV);            // 0xD5
	SSD1306_command(0x80);                                  // the suggested ratio 0x80

	SSD1306_command(SSD1306_SETMULTIPLEX);                  // 0xA8
	SSD1306_command(SSD1306_LCDHEIGHT - 1);

	SSD1306_command(SSD1306_SETDISPLAYOFFSET);              // 0xD3
	SSD1306_command(0x0);                                   // no offset
	SSD1306_command(SSD1306_SETSTARTLINE | 0x0);            // line #0
	SSD1306_command(SSD1306_CHARGEPUMP);                    // 0x8D
	if (vccstate == SSD1306_EXTERNALVCC)
		SSD1306_command(0x10);
	else
		SSD1306_command(0x14);
	
	SSD1306_command(SSD1306_MEMORYMODE);                    // 0x20
	SSD1306_command(0x00);                                  // 0x0 act like ks0108
	SSD1306_command(SSD1306_SEGREMAP | 0x1);
	SSD1306_command(SSD1306_COMSCANDEC);

	#if defined SSD1306_128_32
		SSD1306_command(SSD1306_SETCOMPINS);                    // 0xDA
		SSD1306_command(0x02);
		SSD1306_command(SSD1306_SETCONTRAST);                   // 0x81
		SSD1306_command(0x8F);
	#elif defined SSD1306_128_64
		SSD1306_command(SSD1306_SETCOMPINS);                    // 0xDA
		SSD1306_command(0x12);
		SSD1306_command(SSD1306_SETCONTRAST);                   // 0x81
		
		if (vccstate == SSD1306_EXTERNALVCC)
			SSD1306_command(0x9F);
		else
			SSD1306_command(0xCF);
	#elif defined SSD1306_96_16
		SSD1306_command(SSD1306_SETCOMPINS);                    // 0xDA
		SSD1306_command(0x2);   //ada x12
		SSD1306_command(SSD1306_SETCONTRAST);                   // 0x81
		if (vccstate == SSD1306_EXTERNALVCC)
			SSD1306_command(0x10);
		else
			SSD1306_command(0xAF);
	#endif

	SSD1306_command(SSD1306_SETPRECHARGE);                  // 0xd9
	if (vccstate == SSD1306_EXTERNALVCC)
		SSD1306_command(0x22);
	else
		SSD1306_command(0xF1);
	
	SSD1306_command(SSD1306_SETVCOMDETECT);                 // 0xDB
	SSD1306_command(0x40);
	SSD1306_command(SSD1306_DISPLAYALLON_RESUME);           // 0xA4
	SSD1306_command(SSD1306_NORMALDISPLAY);                 // 0xA6
	
	SSD1306_command(SSD1306_DEACTIVATE_SCROLL);

	SSD1306_command(SSD1306_DISPLAYON);						// Turn on :D
}

void SSD1306_command(uint8_t c)
{
    uint8_t data[] = {((_i2caddr << 1) | I2C_MASTER_WRITE), 0x00, c};

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write(cmd, data, 3, ACK_CHECK_DIS);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, 10/portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK) {
        ESP_LOGI("DSP", "CMD FAIL %d", ret);
    }
}

void SSD1306_buffer(uint8_t* buffer, int length)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, ((_i2caddr << 1) | I2C_MASTER_WRITE), ACK_CHECK_DIS);
    i2c_master_write(cmd, buffer, length, ACK_CHECK_DIS);
    i2c_master_stop(cmd);

    /*esp_err_t ret =*/ i2c_master_cmd_begin(i2c_port, cmd, 10/portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);

    // Ignore ret because it takes long it's always a timeout (???)
}

void SSD1306_drawPixel(int16_t x, int16_t y, uint16_t color) 
{
	if ((x < 0) || (x >= SSD1306_LCDWIDTH) || (y < 0) || (y >= SSD1306_LCDHEIGHT))
		return;

    int index = x + (y / 8) * SSD1306_LCDWIDTH;
    switch (color)
    {
		  case WHITE:   buffer[index] |=  (1 << (y&7)); break;
		  case BLACK:   buffer[index] &= ~(1 << (y&7)); break;
		  case INVERSE: buffer[index] ^=  (1 << (y&7)); break;
    }
}


void SSD1306_invertDisplay(uint8_t i)
{
	if (i)
		SSD1306_command(SSD1306_INVERTDISPLAY);
	else
		SSD1306_command(SSD1306_NORMALDISPLAY);
}

void SSD1306_dim(bool dim)
{
	uint8_t contrast;

	if (dim)
	{
		contrast = 0; // Dimmed display
	}
	else 
	{
		if (_vccstate == SSD1306_EXTERNALVCC)
		{
			contrast = 0x9F;
		}
		else 
		{
			contrast = 0xCF;
		}
	}
	
	SSD1306_command(SSD1306_SETCONTRAST);
	SSD1306_command(contrast);
}

void SSD1306_display(void)
{
	SSD1306_command(SSD1306_COLUMNADDR);
	SSD1306_command(0);   // Column start address (0 = reset)
	SSD1306_command(SSD1306_LCDWIDTH-1); // Column end address (127 = reset)

	SSD1306_command(SSD1306_PAGEADDR);
	SSD1306_command(0); // Page start address (0 = reset)
	#if SSD1306_LCDHEIGHT == 64
		SSD1306_command(7); // Page end address
	#endif
	#if SSD1306_LCDHEIGHT == 32
		SSD1306_command(3); // Page end address
	#endif
	#if SSD1306_LCDHEIGHT == 16
		SSD1306_command(1); // Page end address
	#endif

    // I2C
    for (uint16_t i = 0; i < (SSD1306_LCDWIDTH*SSD1306_LCDHEIGHT/8); i++)
	{
		uint8_t tmpBuf[17];
		// SSD1306_SETSTARTLINE
		tmpBuf[0] = 0x40;
		// data
		for (uint8_t j = 0; j < 16; j++) {
			tmpBuf[j+1] = buffer[i];
			i++;
		}
		i--;
		
		SSD1306_buffer(tmpBuf, sizeof(tmpBuf));
    }
}

void SSD1306_clearDisplay(void)
{
	memset(buffer, 0, (SSD1306_LCDWIDTH*SSD1306_LCDHEIGHT/8));
}

void SSD1306_drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color)
{
	// Do bounds/limit checks
	if(y < 0 || y >= SSD1306_LCDHEIGHT) { return; }

	// make sure we don't try to draw below 0
	if(x < 0) {
		w += x;
		x = 0;
	}

	// make sure we don't go off the edge of the display
	if( (x + w) > SSD1306_LCDWIDTH) {
		w = (SSD1306_LCDWIDTH - x);
	}

	// if our width is now negative, punt
	if(w <= 0) { return; }

	// set up the pointer for  movement through the buffer
	register uint8_t *pBuf = buffer;
	// adjust the buffer pointer for the current row
	pBuf += ((y/8) * SSD1306_LCDWIDTH);
	// and offset x columns in
	pBuf += x;

	register uint8_t mask = 1 << (y&7);

	switch (color)
	{
		case WHITE:     while(w--) { *pBuf++ |= mask; }; break;
		case BLACK: 	mask = ~mask;   while(w--) { *pBuf++ &= mask; }; break;
		case INVERSE:   while(w--) { *pBuf++ ^= mask; }; break;
	}
}

void SSD1306_drawFastVLine(int16_t x, int16_t __y, int16_t __h, uint16_t color)
{
	// do nothing if we're off the left or right side of the screen
	if(x < 0 || x >= SSD1306_LCDWIDTH) { return; }

	// make sure we don't try to draw below 0
	if(__y < 0) {
		// __y is negative, this will subtract enough from __h to account for __y being 0
		__h += __y;
		__y = 0;
	}

	// make sure we don't go past the height of the display
	if( (__y + __h) > SSD1306_LCDHEIGHT) {
		__h = (SSD1306_LCDHEIGHT - __y);
	}

	// if our height is now negative, punt
	if(__h <= 0) {
		return;
	}

	// this display doesn't need ints for coordinates, use local byte registers for faster juggling
	register uint8_t y = __y;
	register uint8_t h = __h;

	// set up the pointer for fast movement through the buffer
	register uint8_t *pBuf = buffer;
	// adjust the buffer pointer for the current row
	pBuf += ((y/8) * SSD1306_LCDWIDTH);
	// and offset x columns in
	pBuf += x;
	
	// do the first partial byte, if necessary - this requires some masking
	register uint8_t mod = (y&7);
	if (mod) {
		// mask off the high n bits we want to set
		mod = 8-mod;

		// note - lookup table results in a nearly 10% performance improvement in fill* functions
		// register uint8_t mask = ~(0xFF >> (mod));
		static uint8_t premask[8] = {0x00, 0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE };
		register uint8_t mask = premask[mod];

		// adjust the mask if we're not going to reach the end of this byte
		if( h < mod) {
			mask &= (0XFF >> (mod-h));
		}

		switch (color)
		{
			case WHITE:   *pBuf |=  mask;  break;
			case BLACK:   *pBuf &= ~mask;  break;
			case INVERSE: *pBuf ^=  mask;  break;
		}

		// fast exit if we're done here!
		if(h<mod) { return; }

		h -= mod;

		pBuf += SSD1306_LCDWIDTH;
	}

	// write solid bytes while we can - effectively doing 8 rows at a time
	if (h >= 8) {
		if (color == INVERSE)  {          // separate copy of the code so we don't impact performance of the black/white write version with an extra comparison per loop
			do  {
				*pBuf=~(*pBuf);

				// adjust the buffer forward 8 rows worth of data
				pBuf += SSD1306_LCDWIDTH;
				
				// adjust h & y (there's got to be a faster way for me to do this, but this should still help a fair bit for now)
				h -= 8;
			} while(h >= 8);
		}
		else
		{
			// store a local value to work with
			register uint8_t val = (color == WHITE) ? 255 : 0;

			do  {
				// write our value in
				*pBuf = val;
				
				// adjust the buffer forward 8 rows worth of data
				pBuf += SSD1306_LCDWIDTH;

				// adjust h & y (there's got to be a faster way for me to do this, but this should still help a fair bit for now)
				h -= 8;
			} while(h >= 8);
		}
    }

	// now do the final partial byte, if necessary
	if (h) {
		mod = h & 7;
		// this time we want to mask the low bits of the byte, vs the high bits we did above
		// register uint8_t mask = (1 << mod) - 1;
		// note - lookup table results in a nearly 10% performance improvement in fill* functions
		static uint8_t postmask[8] = {0x00, 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F };
		register uint8_t mask = postmask[mod];
		switch (color)
		{
			case WHITE:   *pBuf |=  mask;  break;
			case BLACK:   *pBuf &= ~mask;  break;
			case INVERSE: *pBuf ^=  mask;  break;
		}
	}
}

void SSD1306_fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
	for (int16_t i=x; i<x+w; i++) 
	{
		SSD1306_drawFastVLine(i, y, h, color);
	}
}

void SSD1306_outlineRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    SSD1306_drawFastVLine(x, y, h, color);
    SSD1306_drawFastVLine(x + w, y, h + 1, color);

    SSD1306_drawFastHLine(x, y, w, color);
    SSD1306_drawFastHLine(x, y + h, w, color);
}

void SSD1306_drawChar(uint16_t x, uint16_t y, unsigned char c, uint8_t size, uint8_t color)
{
	if ((x >= SSD1306_LCDWIDTH) || (y >= SSD1306_LCDHEIGHT) || ((x + 5 * size - 1) < 0) || ((y + 8 * size - 1) < 0))
	{
		return;
	}

	for (int8_t i=0; i<6; i++ )
	{
		uint8_t line;
		if (i == 5)
			line = 0x0;
		else
			line = font[(c * 5) + i];
		
		for (int8_t j = 0; j<8; j++) {
			if (line & 0x1) {
				if (size == 1)
					SSD1306_drawPixel(x+i, y+j, color);
				else {
					SSD1306_fillRect(x+(i*size), y+(j*size), size, size, color);
				}
			}
			line >>= 1;
		}
	}
}

void SSD1306_drawText(uint16_t x, uint16_t y, const char* text, uint8_t size, uint8_t color)
{
	int len = strlen(text);
	for (int i = 0; i < len; i++)
	{
		SSD1306_drawChar(x + (size * 6 * i), y, text[i], size, color);
	}
}
