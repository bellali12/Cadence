#ifndef _DISPLAY_EPD_W21_SPI_
#define _DISPLAY_EPD_W21_SPI_
#include "Arduino.h"

// === Platform selection (choose one) =========================================
#define  ESP32S3
//#define  Arduino_UNO
//#define  ESP8266

// === ESP32-S3 pin adaptation =========================================
// Wiring:
// BUSY -> GPIO14 (input)
// RST -> GPIO13 (output)
// DC -> GPIO9 (output)
// CS -> GPIO10 (output)
// SCK -> GPIO12 (hardware SPI, configured by SPI.begin() in .ino)
// MOSI -> GPIO11 (hardware SPI)
#ifdef ESP32S3
#define isEPD_W21_BUSY digitalRead(14)
#define EPD_W21_RST_0  digitalWrite(13, LOW)
#define EPD_W21_RST_1  digitalWrite(13, HIGH)
#define EPD_W21_DC_0   digitalWrite(9,  LOW)
#define EPD_W21_DC_1   digitalWrite(9,  HIGH)
#define EPD_W21_CS_0   digitalWrite(10, LOW)
#define EPD_W21_CS_1   digitalWrite(10, HIGH)
#endif

#ifdef ESP8266
//IO settings
//HSCLK---D5
//HMOSI--D7
#define isEPD_W21_BUSY digitalRead(D0) 
#define EPD_W21_RST_0 digitalWrite(D1,LOW)
#define EPD_W21_RST_1 digitalWrite(D1,HIGH)
#define EPD_W21_DC_0  digitalWrite(D2,LOW)
#define EPD_W21_DC_1  digitalWrite(D2,HIGH)
#define EPD_W21_CS_0 digitalWrite(D4,LOW)
#define EPD_W21_CS_1 digitalWrite(D4,HIGH)

void Sys_run(void);
void LED_run(void);    
#endif 

#ifdef Arduino_UNO
//IO settings
//HSCLK---13
//HMOSI--11
#define isEPD_W21_BUSY digitalRead(4) 
#define EPD_W21_RST_0 digitalWrite(5,LOW)
#define EPD_W21_RST_1 digitalWrite(5,HIGH)
#define EPD_W21_DC_0  digitalWrite(6,LOW)
#define EPD_W21_DC_1  digitalWrite(6,HIGH)
#define EPD_W21_CS_0 digitalWrite(7,LOW)
#define EPD_W21_CS_1 digitalWrite(7,HIGH)
#endif 

void SPI_Write(unsigned char value);
void EPD_W21_WriteDATA(unsigned char datas);
void EPD_W21_WriteCMD(unsigned char command);

#endif 
