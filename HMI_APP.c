#include <avr/io.h>
#include <util/delay.h>
#include "timers.h"
#include "lcd.h"
#include "keypad.h"
#include "uart.h"


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



uint8 PASSWORD_set(void);
void DOOR_open(void);
uint8 PASSWORD_import(void);
void PASSWORD_reset(void);

uint8 password[5];
uint8 check_password[5];
volatile uint8 attempt = 0;

/* Function to implement first step */
uint8 PASSWORD_set(void)
{
	uint8 i;

	UART_clearBuffers();

	LCD_clearScreen();

	LCD_displayString("Enter Pass:");
	LCD_moveCursor(1,0);

	/* Gets number from keypad and save it in an array */
	for(i = 0 ; i < 5 ; i++)
	{
		password[i] = KEYPAD_getPressedKey();
		LCD_displayCharacter('*');
	}

	/* Polling to send password when ENTER is pressed */
	while((KEYPAD_getPressedKey() != '='));

	_delay_ms(10);

	/* Loops on password array sending it by UART */
	for(i  = 0 ; i < 5 ; i++)
	{
		UART_sendByte(password[i]);
		_delay_ms(10);
	}

	LCD_clearScreen();

	LCD_displayStringRowColumn(0,0,"Re-Enter the");
	LCD_displayStringRowColumn(1,0,"same pass:");

	/* Loops on password re-enter */
	for(i = 0 ; i < 5 ; i++)
	{
		check_password[i] = KEYPAD_getPressedKey();
		LCD_displayCharacter('*');
	}

	/* Polling to send password when ENTER is pressed */
	while(KEYPAD_getPressedKey() != '=');

	/* Loops on password array sending it by UART */
	for(i  = 0 ; i < 5 ; i++)
	{
		UART_sendByte(check_password[i]);
		_delay_ms(10);
	}

	/* Determine if passwords MATCH or Don't MATCH */
	return UART_receiveByte();
}

/* Function to send that HOME_ECU is ready for further interactions */
void TIMER_callBack(void)
{
	UART_sendByte(READY);
}

/* Function that simulates openning the door */
void DOOR_open(void)
{
	UART_sendByte(OPEN_DOOR);		/* Send it to CONTROL_ECU to open the door */

	LCD_clearScreen();

	LCD_displayString("Unlocking Door");

	/* Setting timer callback to declare ready */
	TIMER_setCallBack(TIMER_callBack,TIMER1_ID);

	/* Using NORMAL Timer1 */
	Timer_ConfigType Timer_config = {TIMER1_ID,NORMAL,0,58593,F_CPU_1024};
	TIMER_init(&Timer_config);

	while(UART_receiveByte() != ACK);	/* Proceed after receiving ack from CONTROL_ECU */

	/* Alert CONTROL_ECU that there's motion */
	while(( UART_receiveByte() == MOTION))
	{
		LCD_clearScreen();

		LCD_displayString("Wait for people");
		LCD_moveCursor(1,0);
		LCD_displayString("to enter");
	}

	LCD_clearScreen();

	LCD_displayString("Locking Door");

	while(UART_receiveByte() != READY);		/* Wait ready from CONTROL_ECU */
}

/* Function to take password with every operation */
uint8 PASSWORD_import(void)
{
	uint8 i;

	UART_clearBuffers();

	LCD_clearScreen();
	LCD_displayString("Enter old Pass:");
	LCD_moveCursor(1,0);

	/* Loops on password entered */
	for(i = 0 ; i < 5 ; i++)
	{
		password[i] = KEYPAD_getPressedKey();
		LCD_displayCharacter('*');
	}

	/* Polling to send password when ENTER is pressed */
	while((KEYPAD_getPressedKey() != '='));

	/* Send entered password by UART */
	for(i  = 0 ; i < 5 ; i++)
	{
		UART_sendByte(password[i]);
		_delay_ms(10);
	}

	_delay_ms(10);

	/* Return state 0:FAILED 1:SUCCEDDED*/
	i = UART_receiveByte();
	LCD_intgerToString(i);

	return i;
	return SUCCESS;
}

void PASSWORD_reset(void)
{
	_delay_ms(10);

	while(PASSWORD_set() != SUCCESS);
}

int main(void)
{
	uint8 key;

	UART_configType UART_config = {9600,DISABLED,BIT_1,BITS_8};
	UART_init(&UART_config);

	LCD_init();

	SREG |= (1<<7);

	while(PASSWORD_set() == ERROR);

	for(;;)
	{
		LCD_clearScreen();

		LCD_displayString("+ : Open Door");
		LCD_moveCursor(1,0);
		LCD_displayString("- : Change Pass");

		key = KEYPAD_getPressedKey();

		while((key != '+' ) && ( key != '-') )
		{
			key = KEYPAD_getPressedKey();
		}

		_delay_ms(10);
		UART_sendByte(MATCH);
		_delay_ms(10);

		/* Gets out of loop if pass is correct or attempts are 3 */
		while( attempt <3 && PASSWORD_import() == ERROR){
			attempt++;
		}
		if(attempt == 3)
		{
			UART_sendByte(ERROR_);		/* Send ERROR to CONTROL_ECU to alert it */

			LCD_clearScreen();

			LCD_displayString("ERROR!");

			_delay_ms(10);
			while(UART_receiveByte() != READY);		/* Wait until CONTROL is ready */
			_delay_ms(10);

			UART_sendByte(ACK);		/* Send ACK to make CONTROL_ECU reset */

			attempt = 0;
			continue;
		}

		if((key == '+'))
		{
			DOOR_open();
		}
		else if (key == '-')
		{
			UART_sendByte(CHANGE_PASSWORD);
			PASSWORD_reset();
		}
	}
}

