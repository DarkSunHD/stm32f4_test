#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/dma.h>

#include "ws2812.h"
#include "isr.h"
#include "ws2812_var.h"
#include "gpio.h"
#include "timer.h"
#include "dma.h"



//This bit buffer contains the threshold values for the PWM (Timer)
uint16_t bit_buffer[LEDS_BUFFER_SIZE];

//Dead time after all logical highs and lows are send to reset all led. (Make Leds ready for the next transmission)
#define LED_DEAD_TIME (2)




//LEDS_BUFFER_SIZE * clear_cycles ZEROS will be send out by the controller to turn off all leds at the beginning.
// LEDS put out = (LEDS_BUFFER_SIZE / 24 * clear_cyles) [Each led has 24 Bits  8 Red, 8 Blue, 8 Green]
uint16_t clear_cycles = 20;
uint16_t count_clear_cycles = 0;

enum led_stage {
	led_idle,
	led_sending,
	led_clear,
	led_done
};

enum dma_stage {
	dma_ready,
	dma_busy
};

volatile enum led_stage led_stage;
volatile enum dma_stage dma_stage;

struct ws2812 {
	ws2812_led_t *leds;
	int led_count;
	int leds_sent;
} ws2812;

//Electric
void ws2812_dma_setup(void);
void ws2812_dma_start(void);
void ws2812_dma_stop(void);

//Misc Funktions
void fill_buffer(bool b_high);
void fill_low_buffer(void);
void fill_high_buffer(void);
void fill_full_buffer(void);
int get_timing_value(int n);

void ws2812_clear_priv(void);




void ws2812_dma_setup(void) {
	ws2812_dma_init();
	dma_stage = dma_ready;
}	


void ws2812_dma_stop(void) {
	timer_set_oc_value(TIM3, TIM_OC1, 0);
	dma_disable_stream(DMA1, DMA_STREAM2);
	timer_set_oc_value(TIM3, TIM_OC1, 0);
	
	dma_stage = dma_ready;
	led_stage = led_idle;
}


void ws2812_dma_start(void) {
	dma_stage = dma_busy;
	dma_enable_stream(DMA1, DMA_STREAM2);
	
}



void dma1_str2_isr(void) {

    if (dma_get_interrupt_flag(DMA1, DMA_STREAM2, DMA_HTIF) != 0) {
        
	
	if(ws2812.leds_sent < (ws2812.led_count + LED_DEAD_TIME) && led_stage == led_sending){
		fill_low_buffer();
	}else if(ws2812.leds_sent >= (ws2812.led_count + LED_DEAD_TIME) && led_stage == led_sending){
		led_stage = led_done;
	}

	if(led_stage == led_done){
		ws2812_dma_stop();
	}
	dma_clear_interrupt_flags(DMA1, DMA_STREAM2, DMA_HTIF);
    }

    if (dma_get_interrupt_flag(DMA1, DMA_STREAM2, DMA_TCIF) != 0) {
        

	if(ws2812.leds_sent < (ws2812.led_count + LED_DEAD_TIME) && led_stage == led_sending){
		fill_high_buffer();
	}else if(ws2812.leds_sent >= (ws2812.led_count + LED_DEAD_TIME) && led_stage == led_sending){
		led_stage = led_done;
	}

	if(led_stage == led_done){
		ws2812_dma_stop();
	}

	if(count_clear_cycles < clear_cycles){
		count_clear_cycles++;
	}else if(led_stage == led_clear){
		ws2812_dma_stop();
	}
	dma_clear_interrupt_flags(DMA1, DMA_STREAM2, DMA_TCIF);	
    }
}


//75 HIGH 
//29 LOW




//LED FUNKTIONS


void ws2812_clear(ws2812_led_t *leds, int led_count) {

	for(int i = 0; i < led_count; i++){
		leds[i].colors.r = 0;
		leds[i].colors.g = 0;
		leds[i].colors.b = 0;
	}

	ws2812_clear_priv();
}


void ws2812_clear_priv(){

	while(dma_stage != dma_ready || led_stage != led_idle){
		__asm__("nop");		
	}

	for(int i = 0; i < LEDS_BUFFER_SIZE; i++) {
		bit_buffer[i] = 29;
	}

	count_clear_cycles = 0;
	led_stage = led_clear;
	ws2812_dma_start();

}


void ws2812_send(ws2812_led_t *leds, int led_count){

	

	while(dma_stage != dma_ready || led_stage != led_idle){
		__asm__("nop");		
	}

	ws2812.leds = leds;
	ws2812.led_count = led_count;
	ws2812.leds_sent = 0;

	fill_full_buffer();
	led_stage = led_sending;
	ws2812_dma_start();
	
}




//misc functions

void fill_high_buffer(void){
	fill_buffer(true);
}

void fill_low_buffer(void){
	fill_buffer(false);
}

void fill_full_buffer(void){
	fill_buffer(false);
	fill_buffer(true);
}	

void fill_buffer(bool b_high){

	int offset = 0;

	if(b_high){
		offset = LEDS_BUFFER_SIZE / 2;
	}


	for(int i = 0; i < LEDS_BUFFER_SIZE / 48 ; i++){

		if(ws2812.leds_sent < ws2812.led_count){
			bit_buffer[i * 24 + offset +  0] = get_timing_value((ws2812.leds[ws2812.leds_sent].colors.r & 0b10000000) >> 7);
			bit_buffer[i * 24 + offset +  1] = get_timing_value((ws2812.leds[ws2812.leds_sent].colors.r & 0b01000000) >> 6);
			bit_buffer[i * 24 + offset +  2] = get_timing_value((ws2812.leds[ws2812.leds_sent].colors.r & 0b00100000) >> 5);
			bit_buffer[i * 24 + offset +  3] = get_timing_value((ws2812.leds[ws2812.leds_sent].colors.r & 0b00010000) >> 4);
			bit_buffer[i * 24 + offset +  4] = get_timing_value((ws2812.leds[ws2812.leds_sent].colors.r & 0b00001000) >> 3);
			bit_buffer[i * 24 + offset +  5] = get_timing_value((ws2812.leds[ws2812.leds_sent].colors.r & 0b00000100) >> 2);
			bit_buffer[i * 24 + offset +  6] = get_timing_value((ws2812.leds[ws2812.leds_sent].colors.r & 0b00000010) >> 1);
			bit_buffer[i * 24 + offset +  7] = get_timing_value((ws2812.leds[ws2812.leds_sent].colors.r & 0b00000001) >> 0);

			bit_buffer[i * 24 + offset +  8] = get_timing_value((ws2812.leds[ws2812.leds_sent].colors.g & 0b10000000) >> 7);
			bit_buffer[i * 24 + offset +  9] = get_timing_value((ws2812.leds[ws2812.leds_sent].colors.g & 0b01000000) >> 6);
			bit_buffer[i * 24 + offset + 10] = get_timing_value((ws2812.leds[ws2812.leds_sent].colors.g & 0b00100000) >> 5);
			bit_buffer[i * 24 + offset + 11] = get_timing_value((ws2812.leds[ws2812.leds_sent].colors.g & 0b00010000) >> 4);
			bit_buffer[i * 24 + offset + 12] = get_timing_value((ws2812.leds[ws2812.leds_sent].colors.g & 0b00001000) >> 3);
			bit_buffer[i * 24 + offset + 13] = get_timing_value((ws2812.leds[ws2812.leds_sent].colors.g & 0b00000100) >> 2);
			bit_buffer[i * 24 + offset + 14] = get_timing_value((ws2812.leds[ws2812.leds_sent].colors.g & 0b00000010) >> 1);
			bit_buffer[i * 24 + offset + 15] = get_timing_value((ws2812.leds[ws2812.leds_sent].colors.g & 0b00000001) >> 0);

			bit_buffer[i * 24 + offset + 16] = get_timing_value((ws2812.leds[ws2812.leds_sent].colors.b & 0b10000000) >> 7);
 			bit_buffer[i * 24 + offset + 17] = get_timing_value((ws2812.leds[ws2812.leds_sent].colors.b & 0b01000000) >> 6);
			bit_buffer[i * 24 + offset + 18] = get_timing_value((ws2812.leds[ws2812.leds_sent].colors.b & 0b00100000) >> 5);
			bit_buffer[i * 24 + offset + 19] = get_timing_value((ws2812.leds[ws2812.leds_sent].colors.b & 0b00010000) >> 4);
			bit_buffer[i * 24 + offset + 20] = get_timing_value((ws2812.leds[ws2812.leds_sent].colors.b & 0b00001000) >> 3);
			bit_buffer[i * 24 + offset + 21] = get_timing_value((ws2812.leds[ws2812.leds_sent].colors.b & 0b00000100) >> 2);
			bit_buffer[i * 24 + offset + 22] = get_timing_value((ws2812.leds[ws2812.leds_sent].colors.b & 0b00000010) >> 1);
			bit_buffer[i * 24 + offset + 23] = get_timing_value((ws2812.leds[ws2812.leds_sent].colors.b & 0b00000001) >> 0);
				
			
			ws2812.leds_sent++;

		}else if(ws2812.leds_sent < ws2812.led_count + LED_DEAD_TIME){

			bit_buffer[i * 24 + offset] = 0;

			ws2812.leds_sent++;			

		}else{
			bit_buffer[i * 24 + offset] = 0;	

		}


	}

}


int get_timing_value(int n){
	if(n == 1){
		return 75;

	}else{

		return 29;
	}
}

bool ws2812_ready(void){
	if(led_stage == led_idle){
		return true;
	}else{
		return false;	
	}
}


void ws2812_init(void){
	
	ws2812_gpio_init();
	ws2812_timer_init();
	ws2812_dma_setup();
	led_stage = led_idle;

	ws2812_clear_priv();
}


/*
int main(void){

	rcc_clock_setup_hse_3v3(&rcc_hse_8mhz_3v3[RCC_CLOCK_3V3_168MHZ]);

	rcc_periph_clock_enable(RCC_GPIOD);
	gpio_mode_setup(GPIOD, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO12);
	gpio_set_af(GPIOD, GPIO_AF2, GPIO12);


	rcc_periph_clock_enable(RCC_GPIOA);
	gpio_mode_setup(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO6);
	gpio_mode_setup(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO7);

	gpio_set(GPIOA, GPIO6);
	gpio_set(GPIOA, GPIO7);	

	ws2812_timer_init();
	ws2812_dma_setup();
	led_stage = led_idle;

	clear();



	while (1) {
		__asm__("nop");
		
	}
}

*/