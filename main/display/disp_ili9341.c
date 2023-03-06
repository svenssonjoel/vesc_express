/*
	Copyright 2023 Benjamin Vedder	benjamin@vedder.se
	Copyright 2023 Joel Svensson    svenssonjoel@yahoo.se

	This file is part of the VESC firmware.

	The VESC firmware is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    The VESC firmware is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "soc/gpio_struct.h"

#include "disp_ili9341.h"
#include "hwspi.h"
#include "lispif.h"
#include "lispbm.h"


static int display_width = 320;
static int display_height = 240;

// Private variables
static int m_pin_reset = -1;
static int m_pin_dc    = -1;

#define DISP_REG_SET		GPIO.out_w1ts.val
#define DISP_REG_CLR		GPIO.out_w1tc.val

#define COMMAND() 	    (DISP_REG_CLR = 1 << m_pin_dc)
#define DATA() 	        (DISP_REG_SET = 1 << m_pin_dc)

void command_start(uint8_t cmd) {
	COMMAND();
	hwspi_send_data(&cmd, 1);
	DATA();
}

static uint16_t to_disp_color(uint32_t rgb) {
	uint8_t b = (uint8_t)rgb;
	uint8_t g = (uint8_t)(rgb >> 8);
	uint8_t r = (uint8_t)(rgb >> 16);
	r >>= 3;
	g >>= 2;
	b >>= 3;

	uint8_t color_high = 0;
	color_high = r << 3;
	color_high |= (g >> 3);

	uint8_t color_low = 0;
	color_low = g << 5;
	color_low |= b;

	// the order of output is bit 7 - 0 : 15 - 8
	uint16_t color = color_high;
	color |= (((uint16_t)color_low) << 8);
	return color;
}

static void blast_indexed2(uint8_t *data, uint32_t *color_map, uint32_t num_pix) {
	uint16_t colors[2];
	colors[0] = to_disp_color(color_map[0]);
	colors[1] = to_disp_color(color_map[1]);

	command_start(0x2C);
	hwspi_data_stream_start();

	for (int i = 0; i < num_pix; i ++) {
		int byte = i >> 3;
		int bit  = 7 - (i & 0x7);
		if (data[byte] & (1 << bit)) {
			hwspi_data_stream_write((uint8_t)(colors[1]));
			hwspi_data_stream_write((uint8_t)(colors[1] >> 8));
		} else {
			hwspi_data_stream_write((uint8_t)(colors[0]));
			hwspi_data_stream_write((uint8_t)(colors[0] >> 8));
		}
	}

	hwspi_data_stream_finish();
}

static void blast_indexed4(uint8_t *data, uint32_t* color_map, uint32_t num_pix) {
	uint16_t colors[4];
	colors[0] = to_disp_color(color_map[0]);
	colors[1] = to_disp_color(color_map[1]);
	colors[2] = to_disp_color(color_map[2]);
	colors[3] = to_disp_color(color_map[3]);

	const uint8_t indexed4_mask[4] = {0x03, 0x0C, 0x30, 0xC0};
	const uint8_t indexed4_shift[4] = {0, 2, 4, 6};

	command_start(0x2C);
	hwspi_data_stream_start();

	for (int i = 0; i < num_pix; i ++) {
		int byte = i >> 2;
		int mask_ix = (3 - (i & 0x03));
		uint16_t c = colors[(data[byte] & indexed4_mask[mask_ix]) >> indexed4_shift[mask_ix]];
		hwspi_data_stream_write((uint8_t)c);
		hwspi_data_stream_write((uint8_t)(c >> 8));
	}

	hwspi_data_stream_finish();
}

static void blast_rgb332(uint8_t *data, uint32_t num_pix) {
	command_start(0x2C);
	hwspi_data_stream_start();

	for (int i = 0; i < num_pix; i ++) {
		uint8_t pix = data[i];
		uint32_t r = (uint32_t)((pix >> 5) & 0x7);
		uint32_t g = (uint32_t)((pix >> 2) & 0x7);
		uint32_t b = (uint32_t)(pix & 0x3);
		uint32_t rgb888 = r << (16 + 5) | g << (8 + 5) | b << 6;
		uint16_t disp = to_disp_color(rgb888);
		hwspi_data_stream_write((uint8_t)disp);
		hwspi_data_stream_write((uint8_t)(disp >> 8));
	}

	hwspi_data_stream_finish();
}

static void blast_rgb565(uint8_t *data, uint32_t num_pix) {
	command_start(0x2C);
	hwspi_data_stream_start();

	for (int i = 0; i < num_pix; i ++) {
		uint16_t pix = (((uint16_t)data[2 * i]) << 8) | ((uint16_t)data[2 * i + 1]);

		uint32_t r = (uint32_t)(pix >> 11);
		uint32_t g = (uint32_t)((pix >> 5) & 0x3F);
		uint32_t b = (uint32_t)(pix & 0x1F);
		uint32_t rgb888 = r << (16 + 3) | g << (8 + 2) | b << 3;
		uint16_t disp = to_disp_color(rgb888);

		hwspi_data_stream_write((uint8_t)disp);
		hwspi_data_stream_write((uint8_t)(disp >> 8));
	}

	hwspi_data_stream_finish();
}

static void blast_rgb888(uint8_t *data, uint32_t num_pix) {
	command_start(0x2C);
	hwspi_data_stream_start();

	for (int i = 0; i < num_pix; i ++) {
		uint32_t r = data[3 * i];
		uint32_t g = data[3 * i + 1];
		uint32_t b = data[3 * i + 2];

		uint32_t rgb888 = r << 16 | g << 8 | b;
		uint16_t disp = to_disp_color(rgb888);

		hwspi_data_stream_write((uint8_t)disp);
		hwspi_data_stream_write((uint8_t)(disp >> 8));
	}

	hwspi_data_stream_finish();
}

bool disp_ili9341_render_image(image_buffer_t *img, uint32_t *color_map, uint16_t x, uint16_t y) {
	uint16_t cs = x;
	uint16_t ce = x + img->width - 1;
	uint16_t ps = y;
	uint16_t pe = y + img->height - 1;

	if (ce >= display_width || pe >= display_height) {
		return false;
	}

	uint8_t col[4] = {cs >> 8, cs, ce >> 8, ce};
	uint8_t row[4] = {ps >> 8, ps, pe >> 8, pe};

	disp_ili9341_command(0x2A, col, 4);
	disp_ili9341_command(0x2B, row, 4);

	uint32_t num_pix = img->width * img->height;

	hwspi_begin();
	switch(img->fmt) {
	case indexed2:
		blast_indexed2(img->data + img->data_offset, color_map, num_pix);
		break;
	case indexed4:
		blast_indexed4(img->data + img->data_offset, color_map, num_pix);
		break;
	case rgb332:
		blast_rgb332(img->data + img->data_offset, num_pix);
		break;
	case rgb565:
		blast_rgb565(img->data + img->data_offset, num_pix);
		break;
	case rgb888:
		blast_rgb888(img->data + img->data_offset, num_pix);
		break;
	default:
		break;
	}
	hwspi_end();

	return true;
}

void disp_ili9341_clear(uint32_t color) {
	uint16_t clear_color_disp = to_disp_color(color);

	uint16_t cs = 0;
	uint16_t ce = display_width - 1;
	uint16_t ps = 0;
	uint16_t pe = display_height - 1;

	uint8_t col[4] = {cs >> 8, cs, ce >> 8, ce};
	uint8_t row[4] = {ps >> 8, ps, pe >> 8, pe};

	disp_ili9341_command(0x2A, col, 4);
	disp_ili9341_command(0x2B, row, 4);

	hwspi_begin();
	command_start(0x2C);
	hwspi_data_stream_start();
	for (int i = 0; i < (display_width * display_height); i ++) {
		hwspi_data_stream_write((uint8_t)(clear_color_disp));
		hwspi_data_stream_write((uint8_t)(clear_color_disp >> 8));
	}
	hwspi_data_stream_finish();
	hwspi_end();
}

static lbm_value ext_disp_cmd(lbm_value *args, lbm_uint argn) {
	LBM_CHECK_NUMBER_ALL();

	lbm_value res = ENC_SYM_TERROR;

	if (argn > 1) {
		uint8_t cmd = (uint8_t)lbm_dec_as_u32(args[0]);
		uint8_t paras[12];
		for (int i = 0; i < argn - 1; i ++) {
			paras[i] = (uint8_t)lbm_dec_as_u32(args[i + 1]);
		}

		disp_ili9341_command(cmd, paras, argn - 1);

		res = ENC_SYM_TRUE;
	} else if (argn == 1) {
		uint8_t cmd = (uint8_t) lbm_dec_as_u32(args[0]);
		disp_ili9341_command(cmd, 0, 0);
		res = ENC_SYM_TRUE;
	}

	return res;
}

void disp_ili9341_init(int pin_sd0, int pin_clk, int pin_cs, int pin_reset, int pin_dc, int clock_mhz) {
	hwspi_init(clock_mhz, 0, -1, pin_sd0, pin_clk, pin_cs);
	m_pin_reset = pin_reset;
	m_pin_dc    = pin_dc;

	gpio_config_t gpconf = {0};
	gpconf.pin_bit_mask = BIT(m_pin_reset) | BIT(m_pin_dc);
	gpconf.mode = GPIO_MODE_OUTPUT;
	gpconf.pull_down_en = GPIO_PULLDOWN_DISABLE;
	gpconf.pull_up_en = GPIO_PULLUP_DISABLE;
	gpconf.intr_type =  GPIO_INTR_DISABLE;

	gpio_config(&gpconf);

	gpio_set_level(m_pin_reset, 1);
	gpio_set_level(m_pin_dc, 0);

	lbm_add_extension("ext-disp-cmd", ext_disp_cmd);
}


void disp_ili9341_command(uint8_t command, uint8_t *args, int argn) {
	hwspi_begin();
	command_start(command);
	if (args != NULL && argn > 0) {
		hwspi_send_data(args, argn);
	}
	hwspi_end();
}

static uint8_t ili9341_init_sequence[15][7] = {
		{4, 0xCF, 0x00, 0xD9, 0x30},
		{5, 0xED, 0x64, 0x03, 0x12, 0x81},
		{4, 0xE8, 0x85, 0x10, 0x7A},
		{6, 0xCB, 0x39, 0x2C, 0x00, 0x34, 0x02},
		{2, 0xF7, 0x20},
		{3, 0xEA, 0x00, 0x00},
		{2, 0xC0, 0x1B},
		{2, 0xC1, 0x12},
		{3, 0xC5, 0x08, 0x26},
		{2, 0xC7, 0xB7},
		{2, 0x36, 0x08},
		{2, 0x3A, 0x55},
		{3, 0xB1, 0x00, 0x1A},
		{3, 0xB6, 0x0A, 0xA2},
		{2, 0x36, 0x68}
};

void disp_ili9341_reset(void) {
	gpio_set_level(m_pin_reset, 0);
	vTaskDelay(200);
	gpio_set_level(m_pin_reset, 1);
	vTaskDelay(100);

	for (int i = 0; i < 15; i ++) {
		int argn = ili9341_init_sequence[i][0] - 1;
		uint8_t *args = &ili9341_init_sequence[i][2];
		uint8_t  cmd  = ili9341_init_sequence[i][1];
		disp_ili9341_command(cmd, args, argn);
	}
	disp_ili9341_command(0x11, 0, 0);
	vTaskDelay(100);
	disp_ili9341_command(0x29, 0, 0);
	vTaskDelay(100);

	disp_ili9341_clear(0);
}

