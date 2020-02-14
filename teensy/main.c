#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "WProgram.h"
#include "pins_arduino.h"
#include "HardwareSerial.h"

#include "common.h"
#include "comms_UART.h"
#include "comms_485.h"
#include "util.h"
#include "spi.h"
#include "i2c.h"
#include "valve.h"

#define PRESSURE_PERIOD_4  2500
#define PRESSURE_PSHIFT       0
#define LSENSOR_PERIOD_2   5000
#define LSENSOR_PSHIFT     1000
#define COMMS_PERIOD_4     5000
#define COMMS_SLAVE_PERIOD 2000
#define COMMS_PSHIFT        500

const float length_scale[2] = {0.0121669, 0.0121669};
#if BOARD == TEENSY1
const float length_offset[2] = {-12.2, -12.38};
#elif BOARD == TEENSY2
const float length_offset[2] = {-12.49, -12.28};
#elif BOARD == TEENSY3
const float length_offset[2] = {-12.20, -12.30};
#endif

const int pressure_channel[4] = {3, 2, 1, 0};

void pack_valve_state(struct valve_ *valve, struct DCV_ *DCV, struct board_state_ *bstate)
{
	int i;
	for (i = 0; i < 4; i++)
		bstate->valve[i] = valve[i].pos;
	for (i = 0; i < 2; i++)
		bstate->DCV[i] = DCV[i].pos;
}

void unpack_valve_cmd(struct valve_ *valve, struct DCV_ *DCV, struct board_cmd_ *bcmd)
{
	int i;
	for (i = 0; i < 4; i++)
		valve[i].cmd = bcmd->valve[i];
	for (i = 0; i < 2; i++)
		DCV[i].cmd = bcmd->DCV[i];
}

static int pressure_read(int channel, float *val)
{
	const float pressure_scale = 63.1272415218;
	uint8_t buff[2];
	uint16_t tmp;

	if (i2c_read(buff, channel, 0x28, 2))
		return 1;

	tmp = (uint16_t)buff[0] << 8 | (uint16_t)buff[1];
	*val = (float)tmp * pressure_scale;

	return 0;
}

static int length_read(int channel, float *val, float scale, float offset)
{
	uint8_t buff[2];
	uint16_t tmp;

	spi_read(buff, channel, 2);

	tmp = (((buff[0] << (int32_t)8) | buff[1]) >> 1) & 0xFFF;
	*val = (float)tmp * scale + offset;

	return 0;
}

void pressure_advance(float *pressure)
{
	const int sensor_num = 4;
	static int i = sensor_num - 1;
	static uint32_t timer = PRESSURE_PSHIFT;

	if (limit_frequency_us(micros(), &timer, PRESSURE_PERIOD_4))
		return;

	pressure_read(pressure_channel[i], &pressure[i]);
	if (--i < 0)
		i = sensor_num - 1;
}

void length_advance(float *length)
{
	static int i;
	static uint32_t timer = LSENSOR_PSHIFT;

	if (limit_frequency_us(micros(), &timer, LSENSOR_PERIOD_2))
		return;

	length_read(i, &length[i], length_scale[i], length_offset[i]);
	i = !i;
}

void comms_advance(struct board_state_ *bstate, struct board_cmd_ *bcmd, uint8_t *cmd_mode, struct valve_ *valve, struct DCV_ *DCV)
{
	static uint32_t timer = COMMS_PSHIFT;
#if BOARD == TEENSY1
	static int got_startb;
	static int state;

	if (limit_frequency_us(micros(), &timer, COMMS_PERIOD_4))
		return;

	switch (state) {
	default:
	case 0:
		while (!comms_UART_recv_cmd(bcmd, &got_startb, cmd_mode))
			;
		if (*cmd_mode == CMD_VALVE)
			unpack_valve_cmd(valve, DCV, &bcmd[0]);
		state++;
		break;
	case 1:
		comms_485_send(1, &bcmd[1], *cmd_mode);
		comms_485_recv(2, &bstate[2]);
		state++;
		break;
	case 2:
		pack_valve_state(valve, DCV, &bstate[0]);
		comms_UART_send_state(bstate);
		state++;
		break;
	case 3:
		comms_485_send(2, &bcmd[2], *cmd_mode);
		comms_485_recv(1, &bstate[1]);
		state = 0;
	}
#else
	if (limit_frequency_us(micros(), &timer, COMMS_SLAVE_PERIOD))
		return;

	if (!comms_485_slave(&bstate[BOARD], &bcmd[BOARD], cmd_mode)) {
		if (*cmd_mode)
			unpack_valve_cmd(valve, DCV, &bcmd[BOARD]);
		pack_valve_state(valve, DCV, &bstate[BOARD]);
	}
#endif
}

int main(void)
{
	struct board_state_ bstate[3];
	struct board_cmd_ bcmd[3];
	struct valve_ valve[4];
	struct DCV_ DCV[4];
	uint8_t cmd_mode;
	int i;

	memset(bstate, 0, 3 * sizeof(*bstate));
	memset(bcmd, 0, 3 * sizeof(*bcmd));
	memset(valve, 0, 4 * sizeof(*valve));
	memset(DCV, 0, 2 * sizeof(*DCV));

	comms_UART_init();
	comms_485_init();
	i2c_init(400000, 10000);
	spi_init();
	valve_init(valve, DCV);

	while (1) {
		comms_advance(bstate, bcmd, &cmd_mode, valve, DCV);

		pressure_advance(bstate[BOARD].pressure);
		length_advance(bstate[BOARD].length);

		switch (cmd_mode) {
		case CMD_LENGTH:
			valve_control_input(&valve[0], &valve[1], &DCV[0], bcmd[BOARD].length[0], bcmd[BOARD].length[0]);
			valve_control_input(&valve[2], &valve[3], &DCV[1], bcmd[BOARD].length[1], bcmd[BOARD].length[1]);
			break;
		case CMD_VALVE:
			for (i = 0; i < 2; i++)
				DCV_switch(&DCV[i]);
			for (i = 0; i < 4; i++)
				valve_step(&valve[i]);
		}
	}

	return 0;
}
