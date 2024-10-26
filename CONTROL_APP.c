#include <util/delay.h>
#include <avr/interrupt.h>
#include <avr/io.h>
#include "timers.h"
#include "uart.h"
#include "twi.h"
#include "buzzer.h"
#include "motor.h"
#include "external_EEPROM.h"
#include "pir.h"


#define NO_MOTION		0
#define ERROR			0
#define MOTION			1
#define SUCCESS 		1
#define OPEN_DOOR		2
#define CHANGE_PASSWORD 3
#define MATCH 			4
#define ACK   			5
#define READY			6
#define ERROR_			7


uint8 PASSWORD_save(void);
uint8 PASSWORD_changeCheck(void);
void PASSWORD_change(void);
uint8 PASSWORD_check(void);
void  DOOR_open(void);
void DOOR_close(void);
void DOOR_closed(void);
void DOOR_opened(void);
void ERROR_check(void);
void ERROR_processing(void);


uint8 tick = 0;
volatile uint8 password[5];
volatile uint8 check_password[5];
volatile uint8 attempt = 0;

/* Function to save imported password */
uint8 PASSWORD_save(void)
{
	uint8 i;

	UART_clearBuffers();	/* Stop receivig until ack */
	_delay_ms(10);

	/* Receive password sent and saves it in an array */
	for(i = 0 ; i<5 ; i++)
	{
		password[i] = UART_receiveByte();
		_delay_ms(10);
	}

	/* Loops on password re-enter */
	for(i = 0 ; i<5 ; i++)
	{
		check_password[i] = UART_receiveByte();
		_delay_ms(10);
	}

	/* Will save password in memory if they are matched */
	if(PASSWORD_check() == SUCCESS)
	{
		EEPROM_writeArray(0x0311,password,5);

		UART_sendByte(SUCCESS);
		_delay_ms(10);

		return SUCCESS;		/* Send '1' to inform HOME_ECU*/
	}
	else		/* Send '0' to inform HOME_ECU*/
	{
		_delay_ms(10);

		UART_sendByte(ERROR);

		return ERROR;
	}

}

/* Function to check and change password */
uint8 PASSWORD_changeCheck(void)
{
	uint8 i;

	UART_clearBuffers();  /* Stop receivig until ack */

	/* Loops on password re-enter */
	for(i = 0 ; i<5 ; i++)
	{
		check_password[i] = UART_receiveByte();
		_delay_ms(10);
	}

	if(PASSWORD_check() == SUCCESS)
	{
		_delay_ms(10);
		UART_sendByte(SUCCESS);
		_delay_ms(10);

		return SUCCESS;		/* Send '1' to inform HOME_ECU match SUCCEDDED*/
	}
	else
	{
		_delay_ms(10);
		UART_sendByte(ERROR);
		_delay_ms(10);

		return ERROR;		/* Send '0' to inform HOME_ECU if match FAILED*/
	}
}

void PASSWORD_change(void)
{
	_delay_ms(10);

	while(PASSWORD_save() == ERROR); /* Will get out of polling when passwords match */
}

/* Function to check on password */
uint8 PASSWORD_check(void)
{
	/* Loops and checks on two sent passwords */
	for(int i = 0; i < 5 ; i++)
	{
		if(password[i] == check_password[i])
		{
			continue;
		}
		else
		{
			return ERROR;		/* Send '0' to inform HOME_ECU if match FAILED*/
		}
	}
	return SUCCESS;		/* Send '1' to inform HOME_ECU match SUCCEDDED*/
}

/* Function to open the door */
void DOOR_open(void)
{
	/* Setting timer callback to rotate motor */
	TIMER_setCallBack(DOOR_closed,TIMER1_ID);

	/* Using NORMAL Timer1 */
	Timer_ConfigType Timer_config = {TIMER1_ID,NORMAL,0,58593,F_CPU_1024};
	TIMER_init(&Timer_config);

	DcMotor_Rotate(CW,100);
}

/* Function to close the door */
void DOOR_close(void)
{
	/* Setting timer callback to rotate motor */
	TIMER_setCallBack(DOOR_opened,TIMER1_ID);

	/* Using NORMAL Timer1 */
	Timer_ConfigType Timer_config = {TIMER1_ID,NORMAL,0,58593,F_CPU_1024};
	TIMER_init(&Timer_config);

	DcMotor_Rotate(A_CW,100);
}


void DOOR_closed(void)
{
	DcMotor_Rotate(STOP,100);

}
void DOOR_opened(void)
{
	DcMotor_Rotate(STOP,100);
	UART_sendByte(READY);		/* Function to send that CONTROL_ECU is ready for further interactions */
}

/* Function to alert others if password is wrong */
void ERROR_check(void)
{
	Buzzer_on();

	/* Setting timer callback to keep alert for 1 minute */
	TIMER_setCallBack(ERROR_processing,TIMER1_ID);

	/* Using NORMAL Timer1 */
	Timer_ConfigType Timer_config = {TIMER1_ID,NORMAL,0,58593,F_CPU_1024};
	TIMER_init(&Timer_config);

}

void ERROR_processing(void)
{
	tick++;
	if(tick == 5)
	{
		UART_sendByte(READY);		/* Send ready to HOME_ECU for further interactions */

		Buzzer_off();

		tick = 0;
	}
}

int main(void)
{
	uint8 state;

	UART_configType UART_config = {9600,DISABLED,BIT_1,BITS_8};
	UART_init(&UART_config);

	TWI_ConfigType TWI_config = {1,400000};
	TWI_init(&TWI_config);

	PIR_init();
	DcMotor_Init();
	Buzzer_init();

	SREG |= (1<<7);

	/* Stay in step1 if two passwords don't MATCH */
	while(PASSWORD_save() == ERROR);

	for(;;)
	{
		state = UART_receiveByte();

		if((state == MATCH))
		{
			/* If entered password dowsn't MATCH saved one for 3 times */
			while( attempt < 3 && PASSWORD_changeCheck() != SUCCESS )
			{
				attempt++;
			}
			if(attempt == 3)
			{
				while(UART_receiveByte() != ERROR_);	/* Wait until alerted */

				_delay_ms(10);
				ERROR_check();
				_delay_ms(10);

				while(UART_receiveByte() != ACK);		/* Wait until ACK is received */

				attempt = 0;

				continue;
			}

			state = UART_receiveByte();

			if(state == OPEN_DOOR)
			{
				DOOR_open();

				while(UART_receiveByte() != READY);		/* Waiting HOME_ECU to be ready */

				UART_sendByte(ACK);		/* Send ack to alert HONE_ECU that door is open */

				/* Send state of PIR to HOME_ECU */
				while(PIR_getState() == MOTION)
				{
					UART_sendByte(PIR_getState());
				}

				UART_sendByte(NO_MOTION);

				DOOR_close();
			}
			else if(state == CHANGE_PASSWORD)		/* Change password if command received from HOME_ECU */
			{
				PASSWORD_change();
			}

		}
	}
}
