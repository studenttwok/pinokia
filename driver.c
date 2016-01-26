#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <strings.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

#include <errno.h>
#include <stdio.h>

#include <time.h>

#include "gpio.h"
#include "spi.h"
#include "driver.h"

static int lcd_reset(int reset_pin) {
  if (gpio_setup() != 0) {
    perror("Couldn't setup GPIO");
    exit(1);
  }

  gpio_set_output(reset_pin);

  int reset_pin_mask = 1 << reset_pin;
  int delay_between_commands = 200 * 1000;

  gpio_clear(reset_pin_mask);

  usleep(delay_between_commands);

  gpio_set(reset_pin_mask);

  usleep(delay_between_commands);

  return 0;
}

inline static void send(LCD *lcd, uint16_t word) __attribute__((always_inline));
inline static void send_cmd(LCD *lcd, uint8_t cmd) __attribute__((always_inline));
inline static void send_data(LCD *lcd, uint8_t data) __attribute__((always_inline));
static void flush(LCD *lcd);

static void send(LCD *lcd, uint16_t word) {
  lcd->buffer[lcd->buffer_pos++] = word;
  if (lcd->buffer_pos == MAX_BUFFER_SIZE) {
    flush(lcd);
  }
}

static void send_cmd(LCD *lcd, uint8_t cmd) {
  send(lcd, cmd);
}

static void send_data(LCD *lcd, uint8_t data) {
  send(lcd, (uint16_t) data | 0x100);
}

static void flush(LCD *lcd) {
  spi_send_buffer(lcd->fd, lcd->buffer_pos, lcd->buffer);
  lcd->buffer_pos = 0;
}

void lcd_clear(LCD *lcd, int color) {
  uint8_t paset, caset, ramwr;
  uint32_t i;

  if (lcd->type == TYPE_EPSON) {
    paset = PASET;
    caset = CASET;
    ramwr = RAMWR;
  } else {
    paset = PASETP;
    caset = CASETP;
    ramwr = RAMWRP;
  }

  send_cmd(lcd, paset);
  send_data(lcd, 0);
  send_data(lcd, 132);
  send_cmd(lcd, caset);
  send_data(lcd, 0);
  send_data(lcd, 132);

  flush(lcd);

  send_cmd(lcd, ramwr);
  flush(lcd);
  for(i = 0; i < (132 * 132) / 2; i++) {
    send_data(lcd, (color >> 4) & 0xFF);
    send_data(lcd, ((color & 0x0F) << 4) | (color >> 8));
    send_data(lcd, color & 0x0FF);
  }

  flush(lcd);
}

void lcd_set_pixel(LCD *lcd, uint8_t x, uint8_t y, uint16_t color) {
  if (lcd->type == TYPE_EPSON) {
    send_cmd(lcd, PASET); // page start/end ram
    send_data(lcd, x);
    send_data(lcd, ENDPAGE);

    send_cmd(lcd, CASET); // column start/end ram
    send_data(lcd, y);
    send_data(lcd, ENDCOL);

    send_cmd(lcd, RAMWR);

    send_data(lcd, (color >> 4) & 0x00ff);
    send_data(lcd, ((color & 0x0f) << 4) | (color >> 8));
    send_data(lcd, color & 0x0ff);
  } else if (lcd->type == TYPE_PHILIPS) {
    send_cmd(lcd, PASETP); // page start/end ram
    send_data(lcd, x);
    send_data(lcd, x);

    send_cmd(lcd, CASETP); // column start/end ram
    send_data(lcd, y);
    send_data(lcd, y);

    send_cmd(lcd, RAMWRP);

    send_data(lcd, (uint8_t) ((color >> 4) & 0x00FF));
    send_data(lcd, (uint8_t) (((color & 0x0F) << 4) | 0x00));
  }

  flush(lcd);
}


int lcd_init(LCD *lcd, char *dev, int reset_pin, int type) {
  int res = lcd_reset(reset_pin);
  if (res < 0) {
    perror("Failed to reset LCD.");
    return 1;
  }

  bzero(lcd, sizeof(LCD));
  lcd->dev = dev;
  lcd->type = type;
  lcd->fd = spi_init(dev);
  if (lcd->fd < 0)
    return lcd->fd;

  if (type == TYPE_EPSON) {
    send_cmd(lcd, DISCTL); // Display control (0xCA)
    send_data(lcd, 0x0C);  // 12 = 1100 - CL dividing ratio [don't divide] switching period 8H (default)
    send_data(lcd, 0x20);  // nlines/4 - 1 = 132/4 - 1 = 32 duty
    send_data(lcd, 0x00);  // No inversely highlighted lines

    send_cmd(lcd, COMSCN); // common scanning direction (0xBB)
    send_data(lcd, 0x01);  // 1->68, 132<-69 scan direction

    send_cmd(lcd, OSCON); // internal oscialltor ON (0xD1)
    send_cmd(lcd, SLPOUT); // sleep out (0x94)

    send_cmd(lcd, PWRCTR); // power ctrl (0x20)
    send_data(lcd, 0x0F);  // everything on, no external reference resistors

    send_cmd(lcd, DISINV); // invert display mode (0xA7)

    send_cmd(lcd, DATCTL); // data control (0xBC)
    send_data(lcd, 0x03);  // Inverse page address, reverse rotation column address, column scan-direction !!! try 0x01
    send_data(lcd, 0x00);  // normal RGB arrangement
    send_data(lcd, 0x02);  // 16-bit Grayscale Type A (12-bit color)

    send_cmd(lcd, VOLCTR); // electronic volume, this is the contrast/brightness (0x81)
    send_data(lcd, 32);  // volume (contrast) setting - fine tuning, original (0-63)
    send_data(lcd, 3);   // internal resistor ratio - coarse adjustment (0-7)

    send_cmd(lcd, NOP); // nop (0x25)

    usleep(100 * 1000);

    send_cmd(lcd, DISON); // display on (0xAF)
  } else if (type == TYPE_PHILIPS) {
    send_cmd(lcd, SLEEPOUT); // Sleep Out (0x11)
    send_cmd(lcd, BSTRON);   // Booster voltage on (0x03)
    send_cmd(lcd, DISPON);   // Display on (0x29)

    // send_cmd(lcd, INVON);    // Inversion on (0x20)
  
    // 12-bit color pixel format:
    send_cmd(lcd, COLMOD);   // Color interface format (0x3A)
    send_data(lcd, 0x03);        // 0b011 is 12-bit/pixel mode
  
    send_cmd(lcd, MADCTL);   // Memory Access Control(PHILLIPS)
    // if (colorSwap)
    //   send_data(lcd, 0x08);
    // else
      send_data(lcd, 0x00);
  
    send_cmd(lcd, SETCON);   // Set Contrast(PHILLIPS)
    send_data(lcd, 0x30);
  
    send_cmd(lcd, NOPP);     // nop(PHILLIPS)
  }

  flush(lcd);

  return 0;
}

void lcd_dispose(LCD *lcd) {
  close(lcd->fd);
  gpio_shutdown();
}



void lcd_draw_circle(LCD *lcd, uint8_t x0, uint8_t y0, uint8_t radius, uint16_t color) { //Midpoint circle algorithm
    uint8_t f = 1 - radius;
    uint8_t ddF_x = 1;
    uint8_t ddF_y = -2 * radius;
    uint8_t x = 0;
    uint8_t y = radius;

    lcd_set_pixel(lcd, x0, y0 + radius, color);
    lcd_set_pixel(lcd, x0, y0 - radius, color);
    lcd_set_pixel(lcd, x0 + radius, y0, color);
    lcd_set_pixel(lcd, x0 - radius, y0, color);

    while(x < y) {
        if(f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;
        lcd_set_pixel(lcd, x0 + x, y0 + y, color);
        lcd_set_pixel(lcd, x0 - x, y0 + y, color);
        lcd_set_pixel(lcd, x0 + x, y0 - y, color);
        lcd_set_pixel(lcd, x0 - x, y0 - y, color);
        lcd_set_pixel(lcd, x0 + y, y0 + x, color);
        lcd_set_pixel(lcd, x0 - y, y0 + x, color);
        lcd_set_pixel(lcd, x0 + y, y0 - x, color);
        lcd_set_pixel(lcd, x0 - y, y0 - x, color);
    }
}


void lcd_draw_br_line(LCD *lcd, uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint16_t color) { // Bresenham Line Algorithm
    uint8_t x;
    uint8_t Dx = x1 - x0; 
    uint8_t Dy = y1 - y0;
    uint8_t steep = (abs(Dy) >= abs(Dx));
    if (steep) {

        uint8_t a = x0;
        uint8_t b = y0;
        x0=b;
        y0=a;

        a = x1;
        b = y1;
        x1=b;
        y1=a;

       // recompute Dx, Dy after swap
       Dx = x1 - x0;
       Dy = y1 - y0;
   }
   uint8_t xstep = 1;
   if (Dx < 0) {
       xstep = -1;
       Dx = -Dx;
   }
   uint8_t ystep = 1;
   if (Dy < 0) {
       ystep = -1;		
       Dy = -Dy; 
   }
   uint8_t TwoDy = 2*Dy; 
   uint8_t TwoDyTwoDx = TwoDy - 2*Dx; // 2*Dy - 2*Dx
   uint8_t E = TwoDy - Dx; //2*Dy - Dx
   uint8_t y = y0;
   uint8_t xDraw, yDraw;	
   for (x = x0; x != x1; x += xstep) {		
       if (steep) {			
           xDraw = y;
           yDraw = x;
       } else {			
           xDraw = x;
           yDraw = y;
       }
       // plot
       lcd_set_pixel(lcd, xDraw, yDraw, color);
       // next
       if (E > 0) {
           E += TwoDyTwoDx; //E += 2*Dy - 2*Dx;
           y = y + ystep;
       } else {
           E += TwoDy; //E += 2*Dy;
       }
   }
}

void lcd_fill_rectangle(LCD *lcd, uint8_t x0, uint8_t y0, uint8_t width, uint8_t height, uint16_t color) {
    uint8_t i;
    for (i = y0+1; i < y0+height; i++) {
        lcd_draw_br_line(lcd, x0, i, x0+width, i, color);
    }
}

void lcd_draw_rectangle(LCD *lcd, uint8_t x0, uint8_t y0, uint8_t width, uint8_t height, uint16_t color) {
    uint8_t i;
    
    // Draw Top side
    lcd_draw_br_line(lcd, x0, y0, x0+width+1, y0, color);

    // Draw Bottom side 
    lcd_draw_br_line(lcd, x0, y0+height, x0+width+1, y0+height, color);

    // Now the sides
    for (i = y0+1; i < y0+height; i++) {
        lcd_set_pixel(lcd, x0, i, color);
        lcd_set_pixel(lcd, x0 + width, i, color);
    }
}

