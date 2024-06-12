#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#define F_CPU 1200000UL
#include <util/delay.h>
#define NULL ((void*)0)

// LEDs matrixed via 74HC595:
// Data: PINB4
// Clock: PINB2
// Latch: PINB1
//
// H  G  F  E
// |  |  |  |
// o--o--o--o-- A
// o--o--o--o-- B
// o--o--o--o-- C
// o--o--o--o-- D
//
// PWM brightness control on PINB0 (OC0A)

// Switch attached to PINB3, closed shorts to ground

typedef void (*update_fun)(uint8_t frame_no);

typedef struct program_def {
	const uint8_t n_frames;
	const uint8_t frame_wait;
	const update_fun update;
	const uint16_t *frames;
} program_def_t;

void button_check();
void set595(uint8_t b);

uint8_t prog, wait, frame,
	x_shift, y_shift,
	button_state,
	button_debounce;

uint16_t fb;

void h_move(uint8_t frame_no) {
	x_shift = frame_no;
}

void v_move(uint8_t frame_no) {
	y_shift = frame_no;
}

uint16_t lfsr = 0xcafe;

void lfsr_step() {
	uint8_t lsb = lfsr & 1;
	lfsr >>= 1;
	if (lsb) {
		lfsr ^= 0xb400;
	}
}

void starfield(uint8_t frame_no) {
	static uint8_t state = 0;

	switch (state) {
	case 0:
		fb = 1 << (lfsr & 0xF);
		for (uint8_t i = 0; i < 4; i++) {
			lfsr_step();
		}
		break;
	case 1:
		fb = 0;
		break;
	default:
		break;
	}

	state++;
	if (state == 5) {
		state = 0;
	}
}

const uint16_t circle[] PROGMEM = {
	0b0110 << 12 |
	0b1001 << 8  |
	0b1001 << 4  |
	0b0110,
};

const uint16_t X[] PROGMEM = {
	0b1001 << 12 |
	0b0110 << 8  |
	0b0110 << 4  |
	0b1001,
};

const uint16_t v_line[] PROGMEM = {
	0b0001 << 12 |
	0b0001 << 8  |
	0b0001 << 4  |
	0b0001,
};

const uint16_t h_line[] PROGMEM = {
	0b1111 << 12 |
	0b0000 << 8  |
	0b0000 << 4  |
	0b0000,
};

const uint16_t chevron_right[] PROGMEM = {
	0b0001 << 12 |
	0b0010 << 8  |
	0b0010 << 4  |
	0b0001,
};

const uint16_t pulse[] PROGMEM = {
	0b0000 << 12 |
	0b0000 << 8  |
	0b0100 << 4  |
	0b0000,
	
	0b0000 << 12 |
	0b0010 << 8  |
	0b0100 << 4  |
	0b0000,
	
	0b0000 << 12 |
	0b0110 << 8  |
	0b0110 << 4  |
	0b0000,
	
	0b0010 << 12 |
	0b1110 << 8  |
	0b0111 << 4  |
	0b0100,
	
	0b0110 << 12 |
	0b1111 << 8  |
	0b1111 << 4  |
	0b0110,
	
	0b1111 << 12 |
	0b1111 << 8  |
	0b1111 << 4  |
	0b1111,
	
};

const program_def_t programs[] PROGMEM = {
	{ 0x81, 255, NULL, circle },
	{ 0x81, 255, NULL, X },
	{ 0x84, 70,  h_move, v_line },
	{ 0x84, 70,  v_move, h_line },
	{ 0x83, 50,  h_move, chevron_right },
	{ 0x06, 20,  NULL, pulse },
	{ 0x80, 20,  starfield, NULL },
};

#define N_PROGRAMS 7

int main() {
	x_shift = 0;
	y_shift = 0;
	frame = 0;
	button_debounce = 0;

	// Outputs: 0, 1, 2, 4. Inputs: 3.
	DDRB = 0b00010111;
	// Set pull-up on the switch
	PORTB = 0b00001000;
	// Enable interrupts for pin changes
	GIMSK = _BV(PCIE);
	// Enable pin change on PB3
	PCMSK = _BV(PCINT3);

	// Configure Timer/PWM output
	TCCR0A = 0b11000011;  // Set OCR0A on compare match, Fast PWM
	TCNT0 = 0;            // Reset timer
	OCR0A = 51;           // ~20% duty cycle
	TCCR0B = 0b00000001;  // Clock select: CLK_IO w/no scaling

	// Enable interrupts
	sei();

	while (1) {
		for (uint8_t y = 0; y < 4; y++) {
			uint8_t col = (~(1 << y) & 0xF) << 4;
			uint8_t row = (fb >> (4*y)) & 0xF;
			set595(col | row);
			_delay_us(500);
		}

		if (button_debounce > 0) {
			button_debounce--;
		}

		if (wait > 0) {
			if (wait < 255) {
				wait--;
			}
			continue;
		}

		uint8_t n_frames = pgm_read_byte(&programs[prog].n_frames);
		int virtual = n_frames & 0x80;
		n_frames = n_frames & 0x7F;

		if (n_frames > 0) {
			// copy frame data to framebuffer
			uint16_t *frames = pgm_read_ptr(&programs[prog].frames);
			uint16_t f = pgm_read_word(frames + (virtual ? 0 : frame));
			for (uint8_t y = 0; y < 4; y++) {
				uint8_t ty = (y + y_shift) % 4;
				uint8_t row = f >> (4 * ty) & 0xF;
				row = (row >> (4 - x_shift)) | ((row << x_shift) & 0xF);
				fb <<= 4;
				fb |= row;
			}
		}

		frame++;
		if (frame == n_frames) {
			frame = 0;
		}

		update_fun update = pgm_read_ptr(&programs[prog].update);
		if (update != NULL) {
			update(frame);
		}

		wait = pgm_read_byte(&programs[prog].frame_wait);
	}
}

void set595(uint8_t b) {
	// shift eight bits
	for (uint8_t i = 0; i < 8; i++) {
		if ((b >> i) & 1) {
			PORTB |= _BV(PB4);
		} else {
			PORTB &= ~(_BV(PB4));
		}
		PORTB |= _BV(PB2);
		PORTB &= ~(_BV(PB2));
	}

	// latch
	PORTB |= _BV(PB1);
	PORTB &= ~(_BV(PB1));
}

void button_check() {
	if (button_debounce > 0) {
		return;
	}

	uint8_t current_state = PINB & 0b00001000;
	uint8_t pressed = button_state & ~current_state;
	if (pressed & _BV(PB3)) {
		prog++;
		if (prog == N_PROGRAMS) prog = 0;
		wait = 0;
		frame = 0;
		x_shift = 0;
		y_shift = 0;
		button_debounce = 30;
	}
	button_state = current_state;
}

ISR(PCINT0_vect) {
	button_check();
}
