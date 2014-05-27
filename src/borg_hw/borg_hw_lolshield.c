/**
 * @file borg_hw_lolshield.c
 * @brief Driver for Jimmie Rodgers' LoL Shield
 * @author Christian Kroll
 * @author Jimmie Rodgers
 * @date 2014
 * @copyright GNU Public License 2 or later
 * @see http://jimmieprodgers.com/kits/lolshield/
 *
 * This driver is partly based on Jimmie Rodger's LoL Shield Library which
 * is available at https://code.google.com/p/lolshield/ (parts of the file
 * "Charliplexing.cpp" have been incorporated into this file).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include "../config.h"
#include "../makros.h"

#include <stdint.h>
#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/pgmspace.h>
#if NUMPLANE >= 8
#	include <math.h>
#endif
#include "borg_hw.h"

// buffer which holds the currently shown frame
unsigned char pixmap[NUMPLANE][NUM_ROWS][LINEBYTES];

// Number of ticks of the prescaled timer per cycle per frame, based on the
// CPU clock speed and the desired frame rate.
#define FRAMERATE 80UL
#define	TICKS (F_CPU + 6 * (FRAMERATE << SLOWSCALERSHIFT)) / (12 * (FRAMERATE << SLOWSCALERSHIFT))
#define	CUTOFF(scaler)	((128 * 12 - 6) * FRAMERATE * scaler)

#if defined (__AVR_ATmega8__)    || \
    defined (__AVR_ATmega48__)   || \
    defined (__AVR_ATmega48P__)  || \
    defined (__AVR_ATmega88__)   || \
    defined (__AVR_ATmega88P__)  || \
    defined (__AVR_ATmega168__)  || \
    defined (__AVR_ATmega168P__) || \
    defined (__AVR_ATmega328__)  || \
    defined (__AVR_ATmega328P__) || \
    defined (__AVR_ATmega1280__) || \
    defined (__AVR_ATmega2560__)
#	if F_CPU < CUTOFF(8)
#		define FASTPRESCALER (_BV(CS20))                          // 1
#		define SLOWPRESCALER (_BV(CS21))                          // 8
#		define FASTSCALERSHIFT 3
#		define SLOWSCALERSHIFT 3
#	elif F_CPU < CUTOFF(32)
#		define FASTPRESCALER (_BV(CS21))                          // 8
#		define SLOWPRESCALER (_BV(CS21) | _BV(CS20))              // 32
#		define FASTSCALERSHIFT 2
#		define SLOWSCALERSHIFT 5
#	elif F_CPU < CUTOFF(64)
#		define FASTPRESCALER (_BV(CS21))                          // 8
#		define SLOWPRESCALER (_BV(CS22))                          // 64
#		define FASTSCALERSHIFT 3
#		define SLOWSCALERSHIFT 6
#   elif F_CPU < CUTOFF(128)
#		define FASTPRESCALER (_BV(CS21) | _BV(CS20))              // 32
#		define SLOWPRESCALER (_BV(CS22) | _BV(CS20))              // 128
#		define FASTSCALERSHIFT 2
#		define SLOWSCALERSHIFT 7
#	elif F_CPU < CUTOFF(256)
#		define FASTPRESCALER (_BV(CS21) | _BV(CS20))              // 32
#		define SLOWPRESCALER (_BV(CS22) | _BV(CS21))              // 256
#		define FASTSCALERSHIFT 3
#		define SLOWSCALERSHIFT 8
#   elif F_CPU < CUTOFF(1024)
#		define FASTPRESCALER (_BV(CS22) | _BV(CS20))              // 128
#		define SLOWPRESCALER (_BV(CS22) | _BV(CS21) | _BV(CS20))  // 1024
#		define FASTSCALERSHIFT 3
#		define SLOWSCALERSHIFT 10
#	else
#		error frame rate is too low
#	endif
#elif defined (__AVR_ATmega32U4__)
#	if F_CPU < CUTOFF(8)
#		define FASTPRESCALER (_BV(WGM12) | _BV(CS10))             // 1
#		define SLOWPRESCALER (_BV(WGM12) | _BV(CS11))             // 8
#		define FASTSCALERSHIFT 3
#		define SLOWSCALERSHIFT 3
#	elif F_CPU < CUTOFF(64)
#		define FASTPRESCALER (_BV(WGM12) | _BV(CS11))             // 8
#		define SLOWPRESCALER (_BV(WGM12) | _BV(CS11) | _BV(CS10)) // 64
#		define FASTSCALERSHIFT 3
#		define SLOWSCALERSHIFT 6
#	elif F_CPU < CUTOFF(256)
#		define FASTPRESCALER (_BV(WGM12) | _BV(CS11) | _BV(CS10)) // 64
#		define SLOWPRESCALER (_BV(WGM12) | _BV(CS12))             // 256
#		define FASTSCALERSHIFT 2
#		define SLOWSCALERSHIFT 8
#	elif F_CPU < CUTOFF(1024)
#		define FASTPRESCALER (_BV(WGM12) | _BV(CS12))             // 256
#		define SLOWPRESCALER (_BV(WGM12) | _BV(CS12) | _BV(CS10)) // 1024
#		define FASTSCALERSHIFT 2
#		define SLOWSCALERSHIFT 10
#	else
#		error frame rate is too low
#	endif
#else
#   error no support for this chip
#endif


#ifndef BRIGHTNESS
#	define BRIGHTNESS 127 /* full brightness by default */
#elif BRIGHTNESS < 0 || BRIGHTNESS > 127
#	error BRIGHTNESS must be between 0 and 127
#endif

#define BRIGHTNESSPERCENT ((BRIGHTNESS * BRIGHTNESS + 8ul) / 16ul)
#define M (TICKS << FASTSCALERSHIFT) * BRIGHTNESSPERCENT /*10b*/
#define C(x) ((M * (unsigned long)(x * 1024) + (1 << 19)) >> 20) /*10b+10b-20b=0b*/

#if NUMPLANE < 8
uint8_t const prescaler[NUMPLANE + 1] = {
	FASTPRESCALER,
#	if NUMPLANE >= 2
	FASTPRESCALER,
#	endif
#	if NUMPLANE >= 3
	FASTPRESCALER,
#	endif
#	if NUMPLANE >= 4
	FASTPRESCALER,
#	endif
#	if NUMPLANE >= 5
	FASTPRESCALER,
#	endif
#	if NUMPLANE >= 6
	FASTPRESCALER,
#	endif
#	if NUMPLANE >= 7
	FASTPRESCALER,
#	endif
	SLOWPRESCALER
};
#else
uint8_t prescaler[NUMPLANE + 1] = {0};
#endif

uint8_t counts[NUMPLANE + 1] = {0};

/**
 *  Set the overall brightness of the screen from 0 (off) to 127 (full on).
 */
static void setBrightness()
{
	/*   ---- This needs review! Please review. -- thilo  */
	// set up page counts
	uint8_t i;

	// NOTE: The argument of C() is calculated as follows:
	// pow((double)x / (double)NUMPLANE, 1.8) with 0 <= x <= NUMPLANE
	// Changing the scale of 1.8 invalidates any tables above!
#if NUMPLANE < 8
	int const temp_counts[NUMPLANE + 1] = {
		0.000000000000000000000000000,
#	if NUMPLANE == 2
		C(0.287174588749258719033719),
#	elif NUMPLANE == 3
		C(0.138414548846168578011273),
		C(0.481987453865643789008288),
#	elif NUMPLANE == 4
		C(0.082469244423305887448095),
		C(0.287174588749258719033719),
		C(0.595813410589956848895099),
#	elif NUMPLANE == 5
		C(0.055189186458448592775827),
		C(0.192179909437029006191722),
		C(0.398723883569384374148115),
		C(0.669209313658414961523135),
#	elif NUMPLANE == 6
		C(0.039749141141812646682574),
		C(0.138414548846168578011273),
		C(0.287174588749258719033719),
		C(0.481987453865643789008288),
		C(0.720234228706005730202833),
#	elif NUMPLANE == 7
		C(0.030117819624378608378557),
		C(0.104876339357015443964904),
		C(0.217591430058779483625031),
		C(0.365200625214741059210155),
		C(0.545719579451565794947498),
		C(0.757697368024318751444923),
#	endif
		C(1.000000000000000000000000),
	};
#else
#	warning "NUMPLANE >= 8 links floating point stuff into the image"
	// NOTE: Changing "scale" invalidates any tables above!
	const float scale = 1.8f;
	int temp_counts[NUMPLANE + 1] = {0};

	for (i = 1; i < (NUMPLANE + 1); i++) {
		temp_counts[i] = C(pow(i / (float)(NUMPLANE), scale));
	}
#endif

	// Compute on time for each of the pages
	// Use the fast timer; slow timer is only useful for < 3 shades.
	for (i = 0; i < NUMPLANE; i++) {
		int interval = temp_counts[i + 1] - temp_counts[i];
		counts[i] = 256 - (interval ? interval : 1);
#if NUMPLANE >= 8
		prescaler[i] = FASTPRESCALER;
#endif
	}

	// Compute off time
	int interval = TICKS - (temp_counts[i] >> FASTSCALERSHIFT);
	counts[i] = 256 - (interval ? interval : 1);
#if NUMPLANE >= 8
		prescaler[i] = SLOWPRESCALER;
#endif
}

/**
 * Distributes the framebuffer content among current cycle pins.
 * @param cycle The cycle whose pattern should to be composed.
 * @param plane The plane ("page" in LoL Shield lingo) to be drawn.
 */
static void compose_cycle(uint8_t const cycle, uint8_t plane) {
	// pointer to corresponding bitmap
	uint8_t *const p = &pixmap[plane][0][0];

#if defined (__AVR_ATmega1280__) || defined (__AVR_ATmega2560__)
#	ifdef __AVR_ATmega1280__
#		warning "BEWARE: Borgware-2D has not been tested on Arduino Mega 1280!"
#	endif

	// Set sink pin to Vcc/source, turning off current.
	static uint8_t sink_b = 0, sink_e = 0, sink_g = 0, sink_h = 0;
	PINB = sink_b;
	PINE = sink_e;
	PING = sink_g;
	PINH = sink_h;

	DDRB &= ~0xf0;
	DDRE &= ~0x38;
	DDRG &= ~0x20;
	DDRH &= ~0x78;

	static uint8_t const PROGMEM sink_b_cycle[] =
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x20, 0x40, 0x80};
	static uint8_t const PROGMEM sink_e_cycle[] =
		{0x10, 0x20, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	static uint8_t const PROGMEM sink_g_cycle[] =
		{0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	static uint8_t const PROGMEM sink_h_cycle[] =
		{0x00, 0x00, 0x00, 0x00, 0x08, 0x10, 0x20, 0x40, 0x00, 0x00, 0x00, 0x00};

	uint8_t pins_b = sink_b = pgm_read_byte(&sink_b_cycle[cycle]);
	uint8_t pins_e = sink_e = pgm_read_byte(&sink_e_cycle[cycle]);
	uint8_t pins_g = sink_g = pgm_read_byte(&sink_g_cycle[cycle]);
	uint8_t pins_h = sink_h = pgm_read_byte(&sink_h_cycle[cycle]);

	// convert framebuffer to LoL Shield cycles on Arduino Mega 1280/2560
	// (I could have done this with a lookup table, but that would be slower as
	// non-constant bit shifts are quite expensive on AVR)
	// NOTE: (0,0) is UPPER RIGHT in the Borgware realm
	if (plane < NUMPLANE) {
		switch(cycle) {
		case 0:
			pins_b |= (0x02u & p[ 0]) << 6; // x= 1, y= 0, mapped pin D13
			pins_b |= (0x02u & p[ 2]) << 5; // x= 1, y= 1, mapped pin D12
			pins_b |= (0x02u & p[ 4]) << 4; // x= 1, y= 2, mapped pin D11
			pins_b |= (0x02u & p[ 6]) << 3; // x= 1, y= 3, mapped pin D10
			pins_e |= (0x02u & p[16]) << 2; // x= 1, y= 8, mapped pin D5
			pins_h |= (0x02u & p[ 8]) << 5; // x= 1, y= 4, mapped pin D9
			pins_h |= (0x02u & p[10]) << 4; // x= 1, y= 5, mapped pin D8
			pins_h |= (0x02u & p[12]) << 3; // x= 1, y= 6, mapped pin D7
			pins_h |= (0x02u & p[14]) << 2; // x= 1, y= 7, mapped pin D6
			break;
		case 1:
			pins_b |= (0x08u & p[ 0]) << 4; // x= 3, y= 0, mapped pin D13
			pins_b |= (0x08u & p[ 2]) << 3; // x= 3, y= 1, mapped pin D12
			pins_b |= (0x08u & p[ 4]) << 2; // x= 3, y= 2, mapped pin D11
			pins_b |= (0x08u & p[ 6]) << 1; // x= 3, y= 3, mapped pin D10
			pins_e |= (0x08u & p[16]);      // x= 3, y= 8, mapped pin D5
			pins_h |= (0x08u & p[ 8]) << 3; // x= 3, y= 4, mapped pin D9
			pins_h |= (0x08u & p[10]) << 2; // x= 3, y= 5, mapped pin D8
			pins_h |= (0x08u & p[12]) << 1; // x= 3, y= 6, mapped pin D7
			pins_h |= (0x08u & p[14]);      // x= 3, y= 7, mapped pin D6
			break;
		case 2:
			pins_b |= (0x20u & p[ 0]) << 2; // x= 5, y= 0, mapped pin D13
			pins_b |= (0x20u & p[ 2]) << 1; // x= 5, y= 1, mapped pin D12
			pins_b |= (0x20u & p[ 4]);      // x= 5, y= 2, mapped pin D11
			pins_b |= (0x20u & p[ 6]) >> 1; // x= 5, y= 3, mapped pin D10
			pins_e |= (0x20u & p[16]) >> 2; // x= 5, y= 8, mapped pin D5
			pins_h |= (0x20u & p[ 8]) << 1; // x= 5, y= 4, mapped pin D9
			pins_h |= (0x20u & p[10]);      // x= 5, y= 5, mapped pin D8
			pins_h |= (0x20u & p[12]) >> 1; // x= 5, y= 6, mapped pin D7
			pins_h |= (0x20u & p[14]) >> 2; // x= 5, y= 7, mapped pin D6
			break;
		case 3:
			pins_b |= (0x20u & p[ 1]) << 2; // x=13, y= 0, mapped pin D13
			pins_b |= (0x20u & p[ 3]) << 1; // x=13, y= 1, mapped pin D12
			pins_b |= (0x20u & p[ 5]);      // x=13, y= 2, mapped pin D11
			pins_b |= (0x20u & p[ 7]) >> 1; // x=13, y= 3, mapped pin D10
			pins_e |= (0x01u & p[16]) << 4; // x= 0, y= 8, mapped pin D2
			pins_e |= (0x04u & p[16]) << 3; // x= 2, y= 8, mapped pin D3
			pins_g |= (0x10u & p[16]) << 1; // x= 4, y= 8, mapped pin D4
			pins_h |= (0x20u & p[ 9]) << 1; // x=13, y= 4, mapped pin D9
			pins_h |= (0x20u & p[11]);      // x=13, y= 5, mapped pin D8
			pins_h |= (0x20u & p[13]) >> 1; // x=13, y= 6, mapped pin D7
			pins_h |= (0x20u & p[15]) >> 2; // x=13, y= 7, mapped pin D6
			break;
		case 4:
			pins_b |= (0x10u & p[ 1]) << 3; // x=12, y= 0, mapped pin D13
			pins_b |= (0x10u & p[ 3]) << 2; // x=12, y= 1, mapped pin D12
			pins_b |= (0x10u & p[ 5]) << 1; // x=12, y= 2, mapped pin D11
			pins_b |= (0x10u & p[ 7]);      // x=12, y= 3, mapped pin D10
			pins_e |= (0x01u & p[14]) << 4; // x= 0, y= 7, mapped pin D2
			pins_e |= (0x04u & p[14]) << 3; // x= 2, y= 7, mapped pin D3
			pins_e |= (0x20u & p[17]) >> 2; // x=13, y= 8, mapped pin D5
			pins_g |= (0x10u & p[14]) << 1; // x= 4, y= 7, mapped pin D4
			pins_h |= (0x10u & p[ 9]) << 2; // x=12, y= 4, mapped pin D9
			pins_h |= (0x10u & p[11]) << 1; // x=12, y= 5, mapped pin D8
			pins_h |= (0x10u & p[13]);      // x=12, y= 6, mapped pin D7
			break;
		case 5:
			pins_b |= (0x08u & p[ 1]) << 4; // x=11, y= 0, mapped pin D13
			pins_b |= (0x08u & p[ 3]) << 3; // x=11, y= 1, mapped pin D12
			pins_b |= (0x08u & p[ 5]) << 2; // x=11, y= 2, mapped pin D11
			pins_b |= (0x08u & p[ 7]) << 1; // x=11, y= 3, mapped pin D10
			pins_e |= (0x01u & p[12]) << 4; // x= 0, y= 6, mapped pin D2
			pins_e |= (0x04u & p[12]) << 3; // x= 2, y= 6, mapped pin D3
			pins_e |= (0x10u & p[17]) >> 1; // x=12, y= 8, mapped pin D5
			pins_g |= (0x10u & p[12]) << 1; // x= 4, y= 6, mapped pin D4
			pins_h |= (0x08u & p[ 9]) << 3; // x=11, y= 4, mapped pin D9
			pins_h |= (0x08u & p[11]) << 2; // x=11, y= 5, mapped pin D8
			pins_h |= (0x10u & p[15]) >> 1; // x=12, y= 7, mapped pin D6
			break;
		case 6:
			pins_b |= (0x04u & p[ 1]) << 5; // x=10, y= 0, mapped pin D13
			pins_b |= (0x04u & p[ 3]) << 4; // x=10, y= 1, mapped pin D12
			pins_b |= (0x04u & p[ 5]) << 3; // x=10, y= 2, mapped pin D11
			pins_b |= (0x04u & p[ 7]) << 2; // x=10, y= 3, mapped pin D10
			pins_e |= (0x01u & p[10]) << 4; // x= 0, y= 5, mapped pin D2
			pins_e |= (0x04u & p[10]) << 3; // x= 2, y= 5, mapped pin D3
			pins_e |= (0x08u & p[17]);      // x=11, y= 8, mapped pin D5
			pins_g |= (0x10u & p[10]) << 1; // x= 4, y= 5, mapped pin D4
			pins_h |= (0x04u & p[ 9]) << 4; // x=10, y= 4, mapped pin D9
			pins_h |= (0x08u & p[13]) << 1; // x=11, y= 6, mapped pin D7
			pins_h |= (0x08u & p[15]);      // x=11, y= 7, mapped pin D6
			break;
		case 7:
			pins_b |= (0x02u & p[ 1]) << 6; // x= 9, y= 0, mapped pin D13
			pins_b |= (0x02u & p[ 3]) << 5; // x= 9, y= 1, mapped pin D12
			pins_b |= (0x02u & p[ 5]) << 4; // x= 9, y= 2, mapped pin D11
			pins_b |= (0x02u & p[ 7]) << 3; // x= 9, y= 3, mapped pin D10
			pins_e |= (0x01u & p[ 8]) << 4; // x= 0, y= 4, mapped pin D2
			pins_e |= (0x04u & p[ 8]) << 3; // x= 2, y= 4, mapped pin D3
			pins_e |= (0x04u & p[17]) << 1; // x=10, y= 8, mapped pin D5
			pins_g |= (0x10u & p[ 8]) << 1; // x= 4, y= 4, mapped pin D4
			pins_h |= (0x04u & p[11]) << 3; // x=10, y= 5, mapped pin D8
			pins_h |= (0x04u & p[13]) << 2; // x=10, y= 6, mapped pin D7
			pins_h |= (0x04u & p[15]) << 1; // x=10, y= 7, mapped pin D6
			break;
		case 8:
			pins_b |= (0x01u & p[ 1]) << 7; // x= 8, y= 0, mapped pin D13
			pins_b |= (0x01u & p[ 3]) << 6; // x= 8, y= 1, mapped pin D12
			pins_b |= (0x01u & p[ 5]) << 5; // x= 8, y= 2, mapped pin D11
			pins_e |= (0x01u & p[ 6]) << 4; // x= 0, y= 3, mapped pin D2
			pins_e |= (0x02u & p[17]) << 2; // x= 9, y= 8, mapped pin D5
			pins_e |= (0x04u & p[ 6]) << 3; // x= 2, y= 3, mapped pin D3
			pins_g |= (0x10u & p[ 6]) << 1; // x= 4, y= 3, mapped pin D4
			pins_h |= (0x02u & p[ 9]) << 5; // x= 9, y= 4, mapped pin D9
			pins_h |= (0x02u & p[11]) << 4; // x= 9, y= 5, mapped pin D8
			pins_h |= (0x02u & p[13]) << 3; // x= 9, y= 6, mapped pin D7
			pins_h |= (0x02u & p[15]) << 2; // x= 9, y= 7, mapped pin D6
			break;
		case 9:
			pins_b |= (0x01u & p[ 7]) << 4; // x= 8, y= 3, mapped pin D10
			pins_b |= (0x80u & p[ 0]);      // x= 7, y= 0, mapped pin D13
			pins_b |= (0x80u & p[ 2]) >> 1; // x= 7, y= 1, mapped pin D12
			pins_e |= (0x01u & p[ 4]) << 4; // x= 0, y= 2, mapped pin D2
			pins_e |= (0x01u & p[17]) << 3; // x= 8, y= 8, mapped pin D5
			pins_e |= (0x04u & p[ 4]) << 3; // x= 2, y= 2, mapped pin D3
			pins_g |= (0x10u & p[ 4]) << 1; // x= 4, y= 2, mapped pin D4
			pins_h |= (0x01u & p[ 9]) << 6; // x= 8, y= 4, mapped pin D9
			pins_h |= (0x01u & p[11]) << 5; // x= 8, y= 5, mapped pin D8
			pins_h |= (0x01u & p[13]) << 4; // x= 8, y= 6, mapped pin D7
			pins_h |= (0x01u & p[15]) << 3; // x= 8, y= 7, mapped pin D6
			break;
		case 10:
			pins_b |= (0x40u & p[ 0]) << 1; // x= 6, y= 0, mapped pin D13
			pins_b |= (0x80u & p[ 4]) >> 2; // x= 7, y= 2, mapped pin D11
			pins_b |= (0x80u & p[ 6]) >> 3; // x= 7, y= 3, mapped pin D10
			pins_e |= (0x01u & p[ 2]) << 4; // x= 0, y= 1, mapped pin D2
			pins_e |= (0x04u & p[ 2]) << 3; // x= 2, y= 1, mapped pin D3
			pins_e |= (0x80u & p[16]) >> 4; // x= 7, y= 8, mapped pin D5
			pins_g |= (0x10u & p[ 2]) << 1; // x= 4, y= 1, mapped pin D4
			pins_h |= (0x80u & p[ 8]) >> 1; // x= 7, y= 4, mapped pin D9
			pins_h |= (0x80u & p[10]) >> 2; // x= 7, y= 5, mapped pin D8
			pins_h |= (0x80u & p[12]) >> 3; // x= 7, y= 6, mapped pin D7
			pins_h |= (0x80u & p[14]) >> 4; // x= 7, y= 7, mapped pin D6
			break;
		case 11:
			pins_b |= (0x40u & p[ 2]);      // x= 6, y= 1, mapped pin D12
			pins_b |= (0x40u & p[ 4]) >> 1; // x= 6, y= 2, mapped pin D11
			pins_b |= (0x40u & p[ 6]) >> 2; // x= 6, y= 3, mapped pin D10
			pins_e |= (0x01u & p[ 0]) << 4; // x= 0, y= 0, mapped pin D2
			pins_e |= (0x04u & p[ 0]) << 3; // x= 2, y= 0, mapped pin D3
			pins_e |= (0x40u & p[16]) >> 3; // x= 6, y= 8, mapped pin D5
			pins_g |= (0x10u & p[ 0]) << 1; // x= 4, y= 0, mapped pin D4
			pins_h |= (0x40u & p[ 8]);      // x= 6, y= 4, mapped pin D9
			pins_h |= (0x40u & p[10]) >> 1; // x= 6, y= 5, mapped pin D8
			pins_h |= (0x40u & p[12]) >> 2; // x= 6, y= 6, mapped pin D7
			pins_h |= (0x40u & p[14]) >> 3; // x= 6, y= 7, mapped pin D6
			break;
		}
	}

	// Enable pullups (by toggling) on new output pins.
	PINB = PORTB ^ pins_b;
	PINE = PORTE ^ pins_e;
	PING = PORTG ^ pins_g;
	PINH = PORTH ^ pins_h;

	// Set pins to output mode; pullups become Vcc/source.
	DDRB |= pins_b;
	DDRE |= pins_e;
	DDRG |= pins_g;
	DDRH |= pins_h;

	// Set sink pin to GND/sink, turning on current.
	PINB = sink_b;
	PINE = sink_e;
	PING = sink_g;
	PINH = sink_h;
#elif defined (__AVR_ATmega32U4__)
	// Set sink pin to Vcc/source, turning off current.
	static uint8_t sink_b = 0, sink_c = 0, sink_d = 0, sink_e = 0;
	PINB = sink_b;
	PINC = sink_c;
	PIND = sink_d;
	PINE = sink_e;

	DDRB &= ~0xF0;
	DDRC &= ~0xC0;
	DDRD &= ~0xD3;
	DDRE &= ~0x40;

	static uint8_t const PROGMEM sink_b_cycle[] =
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x20, 0x40, 0x80, 0x00, 0x00};
	static uint8_t const PROGMEM sink_c_cycle[] =
		{0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80};
	static uint8_t const PROGMEM sink_d_cycle[] =
		{0x02, 0x01, 0x10, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00};
	static uint8_t const PROGMEM sink_e_cycle[] =
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	uint8_t pins_b = sink_b = pgm_read_byte(&sink_b_cycle[cycle]);
	uint8_t pins_c = sink_c = pgm_read_byte(&sink_c_cycle[cycle]);
	uint8_t pins_d = sink_d = pgm_read_byte(&sink_d_cycle[cycle]);
	uint8_t pins_e = sink_e = pgm_read_byte(&sink_e_cycle[cycle]);

	// convert Borgware-2D framebuffer to LoL Shield cycles on Arduino Leonardo
	// (I could have done this with a lookup table, but that would be slower as
	// non-constant bit shifts are quite expensive on AVR)
	// NOTE: (0,0) is UPPER RIGHT in the Borgware realm
	if (plane < NUMPLANE) {
		switch(cycle) {
		case 0:
			pins_b |= (0x02u & p[ 4]) << 6; // x= 1, y= 2, mapped pin D11
			pins_b |= (0x02u & p[ 6]) << 5; // x= 1, y= 3, mapped pin D10
			pins_b |= (0x02u & p[ 8]) << 4; // x= 1, y= 4, mapped pin D9
			pins_b |= (0x02u & p[10]) << 3; // x= 1, y= 5, mapped pin D8
			pins_c |= (0x02u & p[ 0]) << 6; // x= 1, y= 0, mapped pin D13
			pins_c |= (0x02u & p[16]) << 5; // x= 1, y= 8, mapped pin D5
			pins_d |= (0x02u & p[ 2]) << 5; // x= 1, y= 1, mapped pin D12
			pins_d |= (0x02u & p[14]) << 6; // x= 1, y= 7, mapped pin D6
			pins_e |= (0x02u & p[12]) << 5; // x= 1, y= 6, mapped pin D7
			break;
		case 1:
			pins_b |= (0x08u & p[ 4]) << 4; // x= 3, y= 2, mapped pin D11
			pins_b |= (0x08u & p[ 6]) << 3; // x= 3, y= 3, mapped pin D10
			pins_b |= (0x08u & p[ 8]) << 2; // x= 3, y= 4, mapped pin D9
			pins_b |= (0x08u & p[10]) << 1; // x= 3, y= 5, mapped pin D8
			pins_c |= (0x08u & p[ 0]) << 4; // x= 3, y= 0, mapped pin D13
			pins_c |= (0x08u & p[16]) << 3; // x= 3, y= 8, mapped pin D5
			pins_d |= (0x08u & p[ 2]) << 3; // x= 3, y= 1, mapped pin D12
			pins_d |= (0x08u & p[14]) << 4; // x= 3, y= 7, mapped pin D6
			pins_e |= (0x08u & p[12]) << 3; // x= 3, y= 6, mapped pin D7
			break;
		case 2:
			pins_b |= (0x20u & p[ 4]) << 2; // x= 5, y= 2, mapped pin D11
			pins_b |= (0x20u & p[ 6]) << 1; // x= 5, y= 3, mapped pin D10
			pins_b |= (0x20u & p[ 8]);      // x= 5, y= 4, mapped pin D9
			pins_b |= (0x20u & p[10]) >> 1; // x= 5, y= 5, mapped pin D8
			pins_c |= (0x20u & p[ 0]) << 2; // x= 5, y= 0, mapped pin D13
			pins_c |= (0x20u & p[16]) << 1; // x= 5, y= 8, mapped pin D5
			pins_d |= (0x20u & p[ 2]) << 1; // x= 5, y= 1, mapped pin D12
			pins_d |= (0x20u & p[14]) << 2; // x= 5, y= 7, mapped pin D6
			pins_e |= (0x20u & p[12]) << 1; // x= 5, y= 6, mapped pin D7
			break;
		case 3:
			pins_b |= (0x20u & p[ 5]) << 2; // x=13, y= 2, mapped pin D11
			pins_b |= (0x20u & p[ 7]) << 1; // x=13, y= 3, mapped pin D10
			pins_b |= (0x20u & p[ 9]);      // x=13, y= 4, mapped pin D9
			pins_b |= (0x20u & p[11]) >> 1; // x=13, y= 5, mapped pin D8
			pins_c |= (0x20u & p[ 1]) << 2; // x=13, y= 0, mapped pin D13
			pins_d |= (0x01u & p[16]) << 1; // x= 0, y= 8, mapped pin D2
			pins_d |= (0x04u & p[16]) >> 2; // x= 2, y= 8, mapped pin D3
			pins_d |= (0x10u & p[16]);      // x= 4, y= 8, mapped pin D4
			pins_d |= (0x20u & p[ 3]) << 1; // x=13, y= 1, mapped pin D12
			pins_d |= (0x20u & p[15]) << 2; // x=13, y= 7, mapped pin D6
			pins_e |= (0x20u & p[13]) << 1; // x=13, y= 6, mapped pin D7
			break;
		case 4:
			pins_b |= (0x10u & p[ 5]) << 3; // x=12, y= 2, mapped pin D11
			pins_b |= (0x10u & p[ 7]) << 2; // x=12, y= 3, mapped pin D10
			pins_b |= (0x10u & p[ 9]) << 1; // x=12, y= 4, mapped pin D9
			pins_b |= (0x10u & p[11]);      // x=12, y= 5, mapped pin D8
			pins_c |= (0x10u & p[ 1]) << 3; // x=12, y= 0, mapped pin D13
			pins_c |= (0x20u & p[17]) << 1; // x=13, y= 8, mapped pin D5
			pins_d |= (0x01u & p[14]) << 1; // x= 0, y= 7, mapped pin D2
			pins_d |= (0x04u & p[14]) >> 2; // x= 2, y= 7, mapped pin D3
			pins_d |= (0x10u & p[ 3]) << 2; // x=12, y= 1, mapped pin D12
			pins_d |= (0x10u & p[14]);      // x= 4, y= 7, mapped pin D4
			pins_e |= (0x10u & p[13]) << 2; // x=12, y= 6, mapped pin D7
			break;
		case 5:
			pins_b |= (0x08u & p[ 5]) << 4; // x=11, y= 2, mapped pin D11
			pins_b |= (0x08u & p[ 7]) << 3; // x=11, y= 3, mapped pin D10
			pins_b |= (0x08u & p[ 9]) << 2; // x=11, y= 4, mapped pin D9
			pins_b |= (0x08u & p[11]) << 1; // x=11, y= 5, mapped pin D8
			pins_c |= (0x08u & p[ 1]) << 4; // x=11, y= 0, mapped pin D13
			pins_c |= (0x10u & p[17]) << 2; // x=12, y= 8, mapped pin D5
			pins_d |= (0x01u & p[12]) << 1; // x= 0, y= 6, mapped pin D2
			pins_d |= (0x04u & p[12]) >> 2; // x= 2, y= 6, mapped pin D3
			pins_d |= (0x08u & p[ 3]) << 3; // x=11, y= 1, mapped pin D12
			pins_d |= (0x10u & p[12]);      // x= 4, y= 6, mapped pin D4
			pins_d |= (0x10u & p[15]) << 3; // x=12, y= 7, mapped pin D6
			break;
		case 6:
			pins_b |= (0x04u & p[ 5]) << 5; // x=10, y= 2, mapped pin D11
			pins_b |= (0x04u & p[ 7]) << 4; // x=10, y= 3, mapped pin D10
			pins_b |= (0x04u & p[ 9]) << 3; // x=10, y= 4, mapped pin D9
			pins_c |= (0x04u & p[ 1]) << 5; // x=10, y= 0, mapped pin D13
			pins_c |= (0x08u & p[17]) << 3; // x=11, y= 8, mapped pin D5
			pins_d |= (0x01u & p[10]) << 1; // x= 0, y= 5, mapped pin D2
			pins_d |= (0x04u & p[ 3]) << 4; // x=10, y= 1, mapped pin D12
			pins_d |= (0x04u & p[10]) >> 2; // x= 2, y= 5, mapped pin D3
			pins_d |= (0x08u & p[15]) << 4; // x=11, y= 7, mapped pin D6
			pins_d |= (0x10u & p[10]);      // x= 4, y= 5, mapped pin D4
			pins_e |= (0x08u & p[13]) << 3; // x=11, y= 6, mapped pin D7
			break;
		case 7:
			pins_b |= (0x02u & p[ 5]) << 6; // x= 9, y= 2, mapped pin D11
			pins_b |= (0x02u & p[ 7]) << 5; // x= 9, y= 3, mapped pin D10
			pins_b |= (0x04u & p[11]) << 2; // x=10, y= 5, mapped pin D8
			pins_c |= (0x02u & p[ 1]) << 6; // x= 9, y= 0, mapped pin D13
			pins_c |= (0x04u & p[17]) << 4; // x=10, y= 8, mapped pin D5
			pins_d |= (0x01u & p[ 8]) << 1; // x= 0, y= 4, mapped pin D2
			pins_d |= (0x02u & p[ 3]) << 5; // x= 9, y= 1, mapped pin D12
			pins_d |= (0x04u & p[ 8]) >> 2; // x= 2, y= 4, mapped pin D3
			pins_d |= (0x04u & p[15]) << 5; // x=10, y= 7, mapped pin D6
			pins_d |= (0x10u & p[ 8]);      // x= 4, y= 4, mapped pin D4
			pins_e |= (0x04u & p[13]) << 4; // x=10, y= 6, mapped pin D7
			break;
		case 8:
			pins_b |= (0x01u & p[ 5]) << 7; // x= 8, y= 2, mapped pin D11
			pins_b |= (0x02u & p[ 9]) << 4; // x= 9, y= 4, mapped pin D9
			pins_b |= (0x02u & p[11]) << 3; // x= 9, y= 5, mapped pin D8
			pins_c |= (0x01u & p[ 1]) << 7; // x= 8, y= 0, mapped pin D13
			pins_c |= (0x02u & p[17]) << 5; // x= 9, y= 8, mapped pin D5
			pins_d |= (0x01u & p[ 3]) << 6; // x= 8, y= 1, mapped pin D12
			pins_d |= (0x01u & p[ 6]) << 1; // x= 0, y= 3, mapped pin D2
			pins_d |= (0x02u & p[15]) << 6; // x= 9, y= 7, mapped pin D6
			pins_d |= (0x04u & p[ 6]) >> 2; // x= 2, y= 3, mapped pin D3
			pins_d |= (0x10u & p[ 6]);      // x= 4, y= 3, mapped pin D4
			pins_e |= (0x02u & p[13]) << 5; // x= 9, y= 6, mapped pin D7
			break;
		case 9:
			pins_b |= (0x01u & p[ 7]) << 6; // x= 8, y= 3, mapped pin D10
			pins_b |= (0x01u & p[ 9]) << 5; // x= 8, y= 4, mapped pin D9
			pins_b |= (0x01u & p[11]) << 4; // x= 8, y= 5, mapped pin D8
			pins_c |= (0x01u & p[17]) << 6; // x= 8, y= 8, mapped pin D5
			pins_c |= (0x80u & p[ 0]);      // x= 7, y= 0, mapped pin D13
			pins_d |= (0x01u & p[ 4]) << 1; // x= 0, y= 2, mapped pin D2
			pins_d |= (0x01u & p[15]) << 7; // x= 8, y= 7, mapped pin D6
			pins_d |= (0x04u & p[ 4]) >> 2; // x= 2, y= 2, mapped pin D3
			pins_d |= (0x10u & p[ 4]);      // x= 4, y= 2, mapped pin D4
			pins_d |= (0x80u & p[ 2]) >> 1; // x= 7, y= 1, mapped pin D12
			pins_e |= (0x01u & p[13]) << 6; // x= 8, y= 6, mapped pin D7
			break;
		case 10:
			pins_b |= (0x80u & p[ 4]);      // x= 7, y= 2, mapped pin D11
			pins_b |= (0x80u & p[ 6]) >> 1; // x= 7, y= 3, mapped pin D10
			pins_b |= (0x80u & p[ 8]) >> 2; // x= 7, y= 4, mapped pin D9
			pins_b |= (0x80u & p[10]) >> 3; // x= 7, y= 5, mapped pin D8
			pins_c |= (0x40u & p[ 0]) << 1; // x= 6, y= 0, mapped pin D13
			pins_c |= (0x80u & p[16]) >> 1; // x= 7, y= 8, mapped pin D5
			pins_d |= (0x01u & p[ 2]) << 1; // x= 0, y= 1, mapped pin D2
			pins_d |= (0x04u & p[ 2]) >> 2; // x= 2, y= 1, mapped pin D3
			pins_d |= (0x10u & p[ 2]);      // x= 4, y= 1, mapped pin D4
			pins_d |= (0x80u & p[14]);      // x= 7, y= 7, mapped pin D6
			pins_e |= (0x80u & p[12]) >> 1; // x= 7, y= 6, mapped pin D7
			break;
		case 11:
			pins_b |= (0x40u & p[ 4]) << 1; // x= 6, y= 2, mapped pin D11
			pins_b |= (0x40u & p[ 6]);      // x= 6, y= 3, mapped pin D10
			pins_b |= (0x40u & p[ 8]) >> 1; // x= 6, y= 4, mapped pin D9
			pins_b |= (0x40u & p[10]) >> 2; // x= 6, y= 5, mapped pin D8
			pins_c |= (0x40u & p[16]);      // x= 6, y= 8, mapped pin D5
			pins_d |= (0x01u & p[ 0]) << 1; // x= 0, y= 0, mapped pin D2
			pins_d |= (0x04u & p[ 0]) >> 2; // x= 2, y= 0, mapped pin D3
			pins_d |= (0x10u & p[ 0]);      // x= 4, y= 0, mapped pin D4
			pins_d |= (0x40u & p[ 2]);      // x= 6, y= 1, mapped pin D12
			pins_d |= (0x40u & p[14]) << 1; // x= 6, y= 7, mapped pin D6
			pins_e |= (0x40u & p[12]);      // x= 6, y= 6, mapped pin D7
			break;
		}
	}

	// Enable pullups (by toggling) on new output pins.
	PINB = PORTB ^ pins_b;
	PINC = PORTC ^ pins_c;
	PIND = PORTD ^ pins_d;
	PINE = PORTE ^ pins_e;

	// Set pins to output mode; pullups become Vcc/source.
	DDRB |= pins_b;
	DDRC |= pins_c;
	DDRD |= pins_d;
	DDRE |= pins_e;

	// Set sink pin to GND/sink, turning on current.
	PINB = sink_b;
	PINC = sink_c;
	PIND = sink_d;
	PINE = sink_e;
#else
	// Set sink pin to Vcc/source, turning off current.
	static uint8_t sink_b = 0, sink_d = 0;
	PIND = sink_d;
	PINB = sink_b;

	// Set pins to input mode; Vcc/source become pullups.
	DDRD = 0;
	DDRB = 0;

	static uint8_t const PROGMEM sink_d_cycle[] =
		{0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	static uint8_t const PROGMEM sink_b_cycle[] =
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20};

	uint8_t pins_d = sink_d = pgm_read_byte(&sink_d_cycle[cycle]);
	uint8_t pins_b = sink_b = pgm_read_byte(&sink_b_cycle[cycle]);

	// convert Borgware-2D framebuffer to LoL Shield cycles on Diavolino
	// (I could have done this with a lookup table, but that would be slower as
	// non-constant bit shifts are quite expensive on AVR)
	// NOTE: (0,0) is UPPER RIGHT in the Borgware realm
	if (plane < NUMPLANE) {
		switch(cycle) {
		case 0:
			pins_b |= (0x02u & p[ 0]) << 4; // x= 1, y= 0, mapped pin D13
			pins_b |= (0x02u & p[ 2]) << 3; // x= 1, y= 1, mapped pin D12
			pins_b |= (0x02u & p[ 4]) << 2; // x= 1, y= 2, mapped pin D11
			pins_b |= (0x02u & p[ 6]) << 1; // x= 1, y= 3, mapped pin D10
			pins_b |= (0x02u & p[ 8]);      // x= 1, y= 4, mapped pin D9
			pins_b |= (0x02u & p[10]) >> 1; // x= 1, y= 5, mapped pin D8
			pins_d |= (0x02u & p[12]) << 6; // x= 1, y= 6, mapped pin D7
			pins_d |= (0x02u & p[14]) << 5; // x= 1, y= 7, mapped pin D6
			pins_d |= (0x02u & p[16]) << 4; // x= 1, y= 8, mapped pin D5
			break;
		case 1:
			pins_b |= (0x08u & p[ 0]) << 2; // x= 3, y= 0, mapped pin D13
			pins_b |= (0x08u & p[ 2]) << 1; // x= 3, y= 1, mapped pin D12
			pins_b |= (0x08u & p[ 4]);      // x= 3, y= 2, mapped pin D11
			pins_b |= (0x08u & p[ 6]) >> 1; // x= 3, y= 3, mapped pin D10
			pins_b |= (0x08u & p[ 8]) >> 2; // x= 3, y= 4, mapped pin D9
			pins_b |= (0x08u & p[10]) >> 3; // x= 3, y= 5, mapped pin D8
			pins_d |= (0x08u & p[12]) << 4; // x= 3, y= 6, mapped pin D7
			pins_d |= (0x08u & p[14]) << 3; // x= 3, y= 7, mapped pin D6
			pins_d |= (0x08u & p[16]) << 2; // x= 3, y= 8, mapped pin D5
			break;
		case 2:
			pins_b |= (0x20u & p[ 0]);      // x= 5, y= 0, mapped pin D13
			pins_b |= (0x20u & p[ 2]) >> 1; // x= 5, y= 1, mapped pin D12
			pins_b |= (0x20u & p[ 4]) >> 2; // x= 5, y= 2, mapped pin D11
			pins_b |= (0x20u & p[ 6]) >> 3; // x= 5, y= 3, mapped pin D10
			pins_b |= (0x20u & p[ 8]) >> 4; // x= 5, y= 4, mapped pin D9
			pins_b |= (0x20u & p[10]) >> 5; // x= 5, y= 5, mapped pin D8
			pins_d |= (0x20u & p[12]) << 2; // x= 5, y= 6, mapped pin D7
			pins_d |= (0x20u & p[14]) << 1; // x= 5, y= 7, mapped pin D6
			pins_d |= (0x20u & p[16]);      // x= 5, y= 8, mapped pin D5
			break;
		case 3:
			pins_b |= (0x20u & p[ 1]);      // x=13, y= 0, mapped pin D13
			pins_b |= (0x20u & p[ 3]) >> 1; // x=13, y= 1, mapped pin D12
			pins_b |= (0x20u & p[ 5]) >> 2; // x=13, y= 2, mapped pin D11
			pins_b |= (0x20u & p[ 7]) >> 3; // x=13, y= 3, mapped pin D10
			pins_b |= (0x20u & p[ 9]) >> 4; // x=13, y= 4, mapped pin D9
			pins_b |= (0x20u & p[11]) >> 5; // x=13, y= 5, mapped pin D8
			pins_d |= (0x01u & p[16]) << 2; // x= 0, y= 8, mapped pin D2
			pins_d |= (0x04u & p[16]) << 1; // x= 2, y= 8, mapped pin D3
			pins_d |= (0x10u & p[16]);      // x= 4, y= 8, mapped pin D4
			pins_d |= (0x20u & p[13]) << 2; // x=13, y= 6, mapped pin D7
			pins_d |= (0x20u & p[15]) << 1; // x=13, y= 7, mapped pin D6
			break;
		case 4:
			pins_b |= (0x10u & p[ 1]) << 1; // x=12, y= 0, mapped pin D13
			pins_b |= (0x10u & p[ 3]);      // x=12, y= 1, mapped pin D12
			pins_b |= (0x10u & p[ 5]) >> 1; // x=12, y= 2, mapped pin D11
			pins_b |= (0x10u & p[ 7]) >> 2; // x=12, y= 3, mapped pin D10
			pins_b |= (0x10u & p[ 9]) >> 3; // x=12, y= 4, mapped pin D9
			pins_b |= (0x10u & p[11]) >> 4; // x=12, y= 5, mapped pin D8
			pins_d |= (0x01u & p[14]) << 2; // x= 0, y= 7, mapped pin D2
			pins_d |= (0x04u & p[14]) << 1; // x= 2, y= 7, mapped pin D3
			pins_d |= (0x10u & p[13]) << 3; // x=12, y= 6, mapped pin D7
			pins_d |= (0x10u & p[14]);      // x= 4, y= 7, mapped pin D4
			pins_d |= (0x20u & p[17]);      // x=13, y= 8, mapped pin D5
			break;
		case 5:
			pins_b |= (0x08u & p[ 1]) << 2; // x=11, y= 0, mapped pin D13
			pins_b |= (0x08u & p[ 3]) << 1; // x=11, y= 1, mapped pin D12
			pins_b |= (0x08u & p[ 5]);      // x=11, y= 2, mapped pin D11
			pins_b |= (0x08u & p[ 7]) >> 1; // x=11, y= 3, mapped pin D10
			pins_b |= (0x08u & p[ 9]) >> 2; // x=11, y= 4, mapped pin D9
			pins_b |= (0x08u & p[11]) >> 3; // x=11, y= 5, mapped pin D8
			pins_d |= (0x01u & p[12]) << 2; // x= 0, y= 6, mapped pin D2
			pins_d |= (0x04u & p[12]) << 1; // x= 2, y= 6, mapped pin D3
			pins_d |= (0x10u & p[12]);      // x= 4, y= 6, mapped pin D4
			pins_d |= (0x10u & p[15]) << 2; // x=12, y= 7, mapped pin D6
			pins_d |= (0x10u & p[17]) << 1; // x=12, y= 8, mapped pin D5
			break;
		case 6:
			pins_b |= (0x04u & p[ 1]) << 3; // x=10, y= 0, mapped pin D13
			pins_b |= (0x04u & p[ 3]) << 2; // x=10, y= 1, mapped pin D12
			pins_b |= (0x04u & p[ 5]) << 1; // x=10, y= 2, mapped pin D11
			pins_b |= (0x04u & p[ 7]);      // x=10, y= 3, mapped pin D10
			pins_b |= (0x04u & p[ 9]) >> 1; // x=10, y= 4, mapped pin D9
			pins_d |= (0x01u & p[10]) << 2; // x= 0, y= 5, mapped pin D2
			pins_d |= (0x04u & p[10]) << 1; // x= 2, y= 5, mapped pin D3
			pins_d |= (0x08u & p[13]) << 4; // x=11, y= 6, mapped pin D7
			pins_d |= (0x08u & p[15]) << 3; // x=11, y= 7, mapped pin D6
			pins_d |= (0x08u & p[17]) << 2; // x=11, y= 8, mapped pin D5
			pins_d |= (0x10u & p[10]);      // x= 4, y= 5, mapped pin D4
			break;
		case 7:
			pins_b |= (0x02u & p[ 1]) << 4; // x= 9, y= 0, mapped pin D13
			pins_b |= (0x02u & p[ 3]) << 3; // x= 9, y= 1, mapped pin D12
			pins_b |= (0x02u & p[ 5]) << 2; // x= 9, y= 2, mapped pin D11
			pins_b |= (0x02u & p[ 7]) << 1; // x= 9, y= 3, mapped pin D10
			pins_b |= (0x04u & p[11]) >> 2; // x=10, y= 5, mapped pin D8
			pins_d |= (0x01u & p[ 8]) << 2; // x= 0, y= 4, mapped pin D2
			pins_d |= (0x04u & p[ 8]) << 1; // x= 2, y= 4, mapped pin D3
			pins_d |= (0x04u & p[13]) << 5; // x=10, y= 6, mapped pin D7
			pins_d |= (0x04u & p[15]) << 4; // x=10, y= 7, mapped pin D6
			pins_d |= (0x04u & p[17]) << 3; // x=10, y= 8, mapped pin D5
			pins_d |= (0x10u & p[ 8]);      // x= 4, y= 4, mapped pin D4
			break;
		case 8:
			pins_b |= (0x01u & p[ 1]) << 5; // x= 8, y= 0, mapped pin D13
			pins_b |= (0x01u & p[ 3]) << 4; // x= 8, y= 1, mapped pin D12
			pins_b |= (0x01u & p[ 5]) << 3; // x= 8, y= 2, mapped pin D11
			pins_b |= (0x02u & p[ 9]);      // x= 9, y= 4, mapped pin D9
			pins_b |= (0x02u & p[11]) >> 1; // x= 9, y= 5, mapped pin D8
			pins_d |= (0x01u & p[ 6]) << 2; // x= 0, y= 3, mapped pin D2
			pins_d |= (0x02u & p[13]) << 6; // x= 9, y= 6, mapped pin D7
			pins_d |= (0x02u & p[15]) << 5; // x= 9, y= 7, mapped pin D6
			pins_d |= (0x02u & p[17]) << 4; // x= 9, y= 8, mapped pin D5
			pins_d |= (0x04u & p[ 6]) << 1; // x= 2, y= 3, mapped pin D3
			pins_d |= (0x10u & p[ 6]);      // x= 4, y= 3, mapped pin D4
			break;
		case 9:
			pins_b |= (0x01u & p[ 7]) << 2; // x= 8, y= 3, mapped pin D10
			pins_b |= (0x01u & p[ 9]) << 1; // x= 8, y= 4, mapped pin D9
			pins_b |= (0x01u & p[11]);      // x= 8, y= 5, mapped pin D8
			pins_b |= (0x80u & p[ 0]) >> 2; // x= 7, y= 0, mapped pin D13
			pins_b |= (0x80u & p[ 2]) >> 3; // x= 7, y= 1, mapped pin D12
			pins_d |= (0x01u & p[ 4]) << 2; // x= 0, y= 2, mapped pin D2
			pins_d |= (0x01u & p[13]) << 7; // x= 8, y= 6, mapped pin D7
			pins_d |= (0x01u & p[15]) << 6; // x= 8, y= 7, mapped pin D6
			pins_d |= (0x01u & p[17]) << 5; // x= 8, y= 8, mapped pin D5
			pins_d |= (0x04u & p[ 4]) << 1; // x= 2, y= 2, mapped pin D3
			pins_d |= (0x10u & p[ 4]);      // x= 4, y= 2, mapped pin D4
			break;
		case 10:
			pins_b |= (0x40u & p[ 0]) >> 1; // x= 6, y= 0, mapped pin D13
			pins_b |= (0x80u & p[ 4]) >> 4; // x= 7, y= 2, mapped pin D11
			pins_b |= (0x80u & p[ 6]) >> 5; // x= 7, y= 3, mapped pin D10
			pins_b |= (0x80u & p[ 8]) >> 6; // x= 7, y= 4, mapped pin D9
			pins_b |= (0x80u & p[10]) >> 7; // x= 7, y= 5, mapped pin D8
			pins_d |= (0x01u & p[ 2]) << 2; // x= 0, y= 1, mapped pin D2
			pins_d |= (0x04u & p[ 2]) << 1; // x= 2, y= 1, mapped pin D3
			pins_d |= (0x10u & p[ 2]);      // x= 4, y= 1, mapped pin D4
			pins_d |= (0x80u & p[12]);      // x= 7, y= 6, mapped pin D7
			pins_d |= (0x80u & p[14]) >> 1; // x= 7, y= 7, mapped pin D6
			pins_d |= (0x80u & p[16]) >> 2; // x= 7, y= 8, mapped pin D5
			break;
		case 11:
			pins_b |= (0x40u & p[ 2]) >> 2; // x= 6, y= 1, mapped pin D12
			pins_b |= (0x40u & p[ 4]) >> 3; // x= 6, y= 2, mapped pin D11
			pins_b |= (0x40u & p[ 6]) >> 4; // x= 6, y= 3, mapped pin D10
			pins_b |= (0x40u & p[ 8]) >> 5; // x= 6, y= 4, mapped pin D9
			pins_b |= (0x40u & p[10]) >> 6; // x= 6, y= 5, mapped pin D8
			pins_d |= (0x01u & p[ 0]) << 2; // x= 0, y= 0, mapped pin D2
			pins_d |= (0x04u & p[ 0]) << 1; // x= 2, y= 0, mapped pin D3
			pins_d |= (0x10u & p[ 0]);      // x= 4, y= 0, mapped pin D4
			pins_d |= (0x40u & p[12]) << 1; // x= 6, y= 6, mapped pin D7
			pins_d |= (0x40u & p[14]);      // x= 6, y= 7, mapped pin D6
			pins_d |= (0x40u & p[16]) >> 1; // x= 6, y= 8, mapped pin D5
			break;
		}
	}

	// Enable pullups on new output pins.
	PORTD = pins_d;
	PORTB = pins_b;
	// Set pins to output mode; pullups become Vcc/source.
	DDRD = pins_d;
	DDRB = pins_b;
	// Set sink pin to GND/sink, turning on current.
	PIND = sink_d;
	PINB = sink_b;
#endif
}

#if !defined (__AVR_ATmega32U4__)
ISR(TIMER2_OVF_vect) {
#else
ISR(TIMER1_COMPA_vect) {
#endif
	// For each cycle, we have potential planes to display. Once every plane has
	// been displayed, then we move on to the next cycle.
	// NOTE: a "cycle" is a subset of LEDs that can be driven at once.

	// 12 Cycles of Matrix
	static uint8_t cycle = 0;

	// planes to display
	// NOTE: a "plane" in the Borgware is the same as a "page" in Jimmie's lib
	static uint8_t plane = 0;

#if defined (__AVR_ATmega48__)   || \
    defined (__AVR_ATmega48P__)  || \
    defined (__AVR_ATmega88__)   || \
    defined (__AVR_ATmega88P__)  || \
    defined (__AVR_ATmega168__)  || \
    defined (__AVR_ATmega168P__) || \
    defined (__AVR_ATmega328__)  || \
    defined (__AVR_ATmega328P__) || \
    defined (__AVR_ATmega1280__) || \
    defined (__AVR_ATmega2560__)
	TCCR2B = prescaler[plane];
#elif defined (__AVR_ATmega8__) || \
      defined (__AVR_ATmega128__)
	TCCR2 = prescaler[page];
#elif defined (__AVR_ATmega32U4__)
	TCCR1B = prescaler[plane];
#endif
#if !defined (__AVR_ATmega32U4__)
	TCNT2 = counts[plane];
#else
	TCNT1 = counts[plane];
#endif

	// distribute framebuffer contents among current cycle pins
	compose_cycle(cycle, plane++);

	if (plane >= (NUMPLANE + 1)) {
		plane = 0;
		cycle++;
		if (cycle >= 12) {
			cycle = 0;
		}
	}
	wdt_reset();
}

void borg_hw_init() {

#if defined (__AVR_ATmega48__)   || \
    defined (__AVR_ATmega48P__)  || \
    defined (__AVR_ATmega88__)   || \
    defined (__AVR_ATmega88P__)  || \
    defined (__AVR_ATmega168__)  || \
    defined (__AVR_ATmega168P__) || \
    defined (__AVR_ATmega328__)  || \
    defined (__AVR_ATmega328P__) || \
    defined (__AVR_ATmega1280__) || \
    defined (__AVR_ATmega2560__)
	TIMSK2 &= ~(_BV(TOIE2) | _BV(OCIE2A));
	TCCR2A &= ~(_BV(WGM21) | _BV(WGM20));
	TCCR2B &= ~_BV(WGM22);
	ASSR &= ~_BV(AS2);
#elif defined (__AVR_ATmega8__)
	TIMSK &= ~(_BV(TOIE2) | _BV(OCIE2));
	TCCR2 &= ~(_BV(WGM21) | _BV(WGM20));
	ASSR &= ~_BV(AS2);
#elif defined (__AVR_ATmega128__)
	TIMSK &= ~(_BV(TOIE2) | _BV(OCIE2));
	TCCR2 &= ~(_BV(WGM21) | _BV(WGM20));
#elif defined (__AVR_ATmega32U4__)
	// The only 8bit timer on the Leonardo is used by default, so we use the 16bit Timer1
	// in CTC mode with a compare value of 256 to achieve the same behaviour.
	TIMSK1 &= ~(_BV(TOIE1) | _BV(OCIE1A));
	TCCR1A &= ~(_BV(WGM10) | _BV(WGM11));
	OCR1A = 256;
#endif

	setBrightness();

	// Then start the display
#if defined (__AVR_ATmega48__)   || \
    defined (__AVR_ATmega48P__)  || \
    defined (__AVR_ATmega88__)   || \
    defined (__AVR_ATmega88P__)  || \
    defined (__AVR_ATmega168__)  || \
    defined (__AVR_ATmega168P__) || \
    defined (__AVR_ATmega328__)  || \
    defined (__AVR_ATmega328P__) || \
    defined (__AVR_ATmega1280__) || \
    defined (__AVR_ATmega2560__)
	TIMSK2 |= _BV(TOIE2);
	TCCR2B = FASTPRESCALER;
#elif defined (__AVR_ATmega8__) || \
      defined (__AVR_ATmega128__)
	TIMSK |= _BV(TOIE2);
	TCCR2 = FASTPRESCALER;
#elif defined (__AVR_ATmega32U4__)
	// Enable output compare match interrupt
	TIMSK1 |= _BV(OCIE1A);
	TCCR1B = FASTPRESCALER;
#endif
	// interrupt ASAP
#if !defined (__AVR_ATmega32U4__)
		TCNT2 = 255;
#else
		TCNT1 = 255;
#endif

	// activate watchdog timer
	wdt_reset();
	wdt_enable(WDTO_15MS); // 15ms watchdog
}
