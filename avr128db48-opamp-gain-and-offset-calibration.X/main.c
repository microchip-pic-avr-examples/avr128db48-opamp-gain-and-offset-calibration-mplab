/*
 * \file main.c
 *
 * \brief avr128db48-opamp-gain-and-offset-calibration
 *
 *  (c) 2020 Microchip Technology Inc. and its subsidiaries.
 *
 *  Subject to your compliance with these terms,you may use this software and
 *  any derivatives exclusively with Microchip products.It is your responsibility
 *  to comply with third party license terms applicable to your use of third party
 *  software (including open source software) that may accompany Microchip software.
 *
 *  THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES, WHETHER
 *  EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY IMPLIED
 *  WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS FOR A
 *  PARTICULAR PURPOSE.
 *
 *  IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE,
 *  INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND
 *  WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP HAS
 *  BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE. TO THE
 *  FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL CLAIMS IN
 *  ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT OF FEES, IF ANY,
 *  THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS SOFTWARE.
 */
#define F_CPU 4000000UL // 4 MHz is default CPU and peripheral frequency

#include <avr/io.h>
#include <util/delay.h>

// To compile the fuse configuration, the FUSES macro is used. The fuse settings are set to the device production values.
FUSES = {
    .WDTCFG = FUSE_WDTCFG_DEFAULT,
    .BODCFG = FUSE_BODCFG_DEFAULT,
    .OSCCFG = FUSE_OSCCFG_DEFAULT,
    .SYSCFG0 = FUSE_SYSCFG0_DEFAULT,
    .SYSCFG1 = FUSE_SYSCFG1_DEFAULT,
    .CODESIZE = FUSE_CODESIZE_DEFAULT,
    .BOOTSIZE = FUSE_BOOTSIZE_DEFAULT,
};
// To compile the lockbits configuration, the LOCKBITS macro is used. The lockbits are set to unlocked.
LOCKBITS = {
    LOCKBITS_DEFAULT,
};

// The VREF (Voltage Reference) peripheral will be set up to generate a
// reference voltage of 2.5 V
#define VREF_REFSEL_CONTROL VREF_REFSEL_2V500_gc
#define VREF_MV 2500.0 // Floating-point value of reference voltage in mV

// The DAC (Digital-to-Analog Converter) has 10-bit resolution and 2^10 = 1024
// possible input values
// DAC outputs of approximately 60 mV and 120 mV will be used to perform a
// two-point gain and offset measurement/calibration of the op amp gain stage
// Here is the calculation of the two DAC data values needed to generate these
// output voltages:
// (60 mV / 2500 mV) * 1024 = 25
// (120 mV / 2500 mV) * 1024 = 49
#define DAC_DATA_A 25
#define DAC_DATA_B 49

// The AVR-DB ADC (Analog-to-Digital Converter) has 12-bit resolution and
// 2^12 = 4096 possible output values
// Determine the number of mV equivalent to one LSB change in the ADC output:
#define MV_PER_ADC_LSB (VREF_MV/4096)

// Number of measurements to average
#define N_AVERAGE 100

void measure(uint16_t dac_data, uint8_t muxpos_dacout, uint8_t muxpos_opout,
int16_t *dac_meas, int16_t *opout_meas){
	
	//Given a DAC setting, this function measures the op amp input and output values
	
	DAC0.DATA = dac_data << DAC_DATA0_bp; // Write a new value to the DAC
	ADC0.MUXPOS = muxpos_dacout; // Configure the ADC input mux to select the DAC output
	_delay_ms(1); // Allow time for the DAC output and ADC input to settle
	
	ADC0.COMMAND = ADC_STCONV_bm; // Start an ADC conversion
	while(ADC0.COMMAND & ADC_STCONV_bm); // Wait for the ADC conversion to complete
	*dac_meas = ADC0.RES; // Save the ADC result as a measurement of the DAC output value
	
	ADC0.MUXPOS = muxpos_opout; // Configure the ADC input mux to select the OPAMP output
	_delay_ms(1); // Allow time for the ADC input to settle
	
	ADC0.COMMAND = ADC_STCONV_bm; // Start an ADC conversion
	while(ADC0.COMMAND & ADC_STCONV_bm); // Wait for the ADC conversion to complete
	*opout_meas = ADC0.RES; // Save ADC result as a measurement of the OPAMP output
	
	return;
}

volatile float meas_gain = 0; // Measured gain
volatile float meas_offset = 0; // Measured input offset in ADC units
volatile float meas_offset_mv = 0; // Measured input offset in mV

int main(void)
{
	uint8_t n;
	int16_t xa, ya, xb, yb;
	int32_t accum_xa, accum_ya, accum_xb, accum_yb;
	float avg_xa, avg_ya, avg_xb, avg_yb;
	
	// Disable digital inputs on OP0 input and output pins
	PORTD.PIN1CTRL = PORT_ISC_INPUT_DISABLE_gc;
	PORTD.PIN2CTRL = PORT_ISC_INPUT_DISABLE_gc;
	PORTD.PIN3CTRL = PORT_ISC_INPUT_DISABLE_gc;
	
	// Disable digital input on DAC output pin
	PORTD.PIN6CTRL = PORT_ISC_INPUT_DISABLE_gc;
	
	// Set up the timebase of the OPAMP peripheral
	OPAMP.TIMEBASE = 3; // Number of CLK_PER cycles that equal one us, minus one (4-1=3)

	//Configure OP0 as non-inverting gain of 16 with DAC as input
	OPAMP.OP0INMUX = OPAMP_OP0INMUX_MUXPOS_DAC_gc | OPAMP_OP0INMUX_MUXNEG_WIP_gc;
	OPAMP.OP0RESMUX = OPAMP_OP0RESMUX_MUXBOT_GND_gc | OPAMP_OP0RESMUX_MUXWIP_WIP7_gc | OPAMP_OP0RESMUX_MUXTOP_OUT_gc;
	// Configure OP0 Control A
	OPAMP.OP0CTRLA = OPAMP_RUNSTBY_bm |  OPAMP_OP0CTRLA_OUTMODE_NORMAL_gc | OPAMP_ALWAYSON_bm;

	// Enable the OPAMP peripheral
	OPAMP.CTRLA = OPAMP_ENABLE_bm;

	// Set up VREF peripheral to generate the same reference for both ADC and DAC
	VREF.ADC0REF = VREF_ALWAYSON_bm | VREF_REFSEL_CONTROL;
	VREF.DAC0REF = VREF_ALWAYSON_bm | VREF_REFSEL_CONTROL;
	
	// Set up DAC peripheral by enabling it and its output pin
	DAC0.CTRLA = DAC_OUTEN_bm | DAC_ENABLE_bm;
	
	// Set up ADC peripheral
	ADC0.CTRLC = ADC_PRESC_DIV4_gc; // Set up ADC prescaler to DIV4
	// so CLK_ADC = 4 MHz / 4 = 1 MHz
	ADC0.CTRLA = ADC_ENABLE_bm; // Enable ADC in single-ended mode
	
	accum_xa = 0; accum_ya = 0; accum_xb = 0; accum_yb = 0; //Reset accumulators
	for (n = 0; n < N_AVERAGE; n++){
		measure(DAC_DATA_A, ADC_MUXPOS_AIN6_gc, ADC_MUXPOS_AIN2_gc, &xa, &ya);
		//                  DACOUT              OP0OUT
		measure(DAC_DATA_B, ADC_MUXPOS_AIN6_gc, ADC_MUXPOS_AIN2_gc, &xb, &yb);
		//                  DACOUT              OP0OUT
		accum_xa += xa; accum_ya += ya; accum_xb += xb; accum_yb += yb; //Add to accumulators
	}
	avg_xa = ((float) accum_xa)/((float) N_AVERAGE);
	avg_ya = ((float) accum_ya)/((float) N_AVERAGE);
	avg_xb = ((float) accum_xb)/((float) N_AVERAGE);
	avg_yb = ((float) accum_yb)/((float) N_AVERAGE);
	
	meas_gain = (avg_yb - avg_ya)/(avg_xb - avg_xa); // Measured gain
	meas_offset = (avg_ya/meas_gain) - avg_xa; // Measured input offset in ADC units
	meas_offset_mv = meas_offset * MV_PER_ADC_LSB; // Measured input offset in mV
	
	// Calibration is complete and the OPAMP is ready to be used by the application,
	// so configure the OPAMP input mux to select the input pin.
	OPAMP.OP0INMUX = OPAMP_OP0INMUX_MUXPOS_INP_gc | OPAMP_OP0INMUX_MUXNEG_WIP_gc;
	
	while(1){
		// Place application here
	}

}