#include "Display_EPD_W21_spi.h"
#include "Display_EPD_W21.h"

void delay_xms(unsigned int xms)
{
  delay(xms);//At least 100ms delay 	
}
////////////////////////////////////E-paper demo//////////////////////////////////////////////////////////
//Busy function
#ifdef ESP8266
void lcd_chkstatus(void)
{
  while(1)
  {  //=0 BUSY
     if(isEPD_W21_BUSY==1) break;
     ESP.wdtFeed(); //Feed dog to prevent system reset
  }
}
#endif
#ifdef Arduino_UNO
void lcd_chkstatus(void)
{
  while(1)
  {  //=0 BUSY
     if(isEPD_W21_BUSY==1) break;
  }
}
#endif
#ifdef ESP32S3
// ESP32-S3 adaptation: read BUSY and continuously log diagnostics
//
// Strategy:
// 1) Print the current BUSY level on entry
// 2) Sample and print once per second (including level-change detection)
// 3) Exit as soon as BUSY reads 1, or when max_ms expires (avoid infinite waiting)
//
// Use these logs to interpret display behavior:
// - BUSY should be 0 during refresh (display busy)
// - BUSY should become 1 after refresh (display idle)
// - If BUSY stays 0 until timeout, the display IC may not have received the refresh command (SPI issue)
// - If BUSY stays 1 and returns immediately, the display IC may not have started refreshing (command ineffective)
static void lcd_chkstatus_max(uint32_t max_ms)
{
  uint32_t t0 = millis();
  uint32_t last_print = t0;
  int last_lvl = digitalRead(14);
  bool became_high = (last_lvl == 1);

  Serial.print("[DBG] chkstatus enter: BUSY=");
  Serial.print(last_lvl);
  Serial.print(" max=");
  Serial.print(max_ms / 1000);
  Serial.println("s");

  while (millis() - t0 < max_ms) {
    int lvl = digitalRead(14);
    if (lvl == 1) {
      became_high = true;
      // BUSY went high; exit immediately
      if (lvl != last_lvl) {
        Serial.print("[DBG] BUSY -> 1 at ");
        Serial.print((millis() - t0) / 1000);
        Serial.println("s");
      }
      return;
    }
    // Print once per second (only while the level remains 0)
    if (millis() - last_print >= 1000) {
      last_print = millis();
      Serial.print("[DBG] t=");
      Serial.print((millis() - t0) / 1000);
      Serial.print("s BUSY=0");
      Serial.println();
    }
    last_lvl = lvl;
    delay(1);
  }
  // Exit on timeout
  Serial.print("[DBG] chkstatus timeout ");
  Serial.print(max_ms / 1000);
  Serial.print("s, became_high=");
  Serial.println(became_high ? "yes" : "no");
}

// Backward compatibility: default short 300 ms wait (after RST/PON)
void lcd_chkstatus(void)
{
  lcd_chkstatus_max(300);
}
#endif
// ESP32-S3 wakeup sequence: waking from deep sleep (0x07 0xA5) requires strict timing
// Specification: hold RST low for >=10 ms (200 ms recommended); after release, wait for BUSY high (up to 200 ms)
// Critical: wait until BUSY goes high before sending further commands, or the display will not respond
static void epd_hardware_reset(void)
{
  Serial.print("[DBG] HW reset start, BUSY=");
  Serial.println(digitalRead(14));

  // Step 1: hold RST low for 200 ms (required for wakeup from deep sleep)
  EPD_W21_RST_0;
  delay_xms(200);
  // Step 2: set RST high
  EPD_W21_RST_1;
  // Step 3: wait for BUSY to go high (display wakeup indicator), up to 500 ms
  {
    uint32_t t0 = millis();
    while (millis() - t0 < 500) {
      if (digitalRead(14) == 1) break;
      delay(1);
    }
    Serial.print("[DBG] HW reset done in ");
    Serial.print(millis() - t0);
    Serial.print("ms, BUSY=");
    Serial.println(digitalRead(14));
  }
  // Wait another 100 ms for the IC to become fully ready
  delay_xms(100);
}

void EPD_init(void)
{
  epd_hardware_reset();

  EPD_W21_WriteCMD(0x4D);
  EPD_W21_WriteDATA(0x78);

  EPD_W21_WriteCMD(0x00);	//PSR
  EPD_W21_WriteDATA(0x0F);
  EPD_W21_WriteDATA(0x29);

  EPD_W21_WriteCMD(0x01);	//PWRR
  EPD_W21_WriteDATA(0x07);
  EPD_W21_WriteDATA(0x00);
  
  EPD_W21_WriteCMD(0x03);	//POFS
  EPD_W21_WriteDATA(0x10);
  EPD_W21_WriteDATA(0x54);
  EPD_W21_WriteDATA(0x44);
  
  EPD_W21_WriteCMD(0x06);	//BTST_P
	EPD_W21_WriteDATA(0x0F);
	EPD_W21_WriteDATA(0x0A);
	EPD_W21_WriteDATA(0x2F);
	EPD_W21_WriteDATA(0x25);
	EPD_W21_WriteDATA(0x22);
	EPD_W21_WriteDATA(0x2E);
	EPD_W21_WriteDATA(0x21);

  EPD_W21_WriteCMD(0x50);	//CDI
  EPD_W21_WriteDATA(0x37);  
  
  EPD_W21_WriteCMD(0x61); //TRES
  EPD_W21_WriteDATA(Source_BITS/256);		// Source_BITS_H
  EPD_W21_WriteDATA(Source_BITS%256);		// Source_BITS_L
  EPD_W21_WriteDATA(Gate_BITS/256);			// Gate_BITS_H
  EPD_W21_WriteDATA(Gate_BITS%256); 		// Gate_BITS_L	
  
  EPD_W21_WriteCMD(0xE3);
  EPD_W21_WriteDATA(0x22);
	
  EPD_W21_WriteCMD(0xB6);
  EPD_W21_WriteDATA(0x6F);
  
  EPD_W21_WriteCMD(0xB4);
  EPD_W21_WriteDATA(0xD0);

  EPD_W21_WriteCMD(0xE9);
  EPD_W21_WriteDATA(0x01); 

  EPD_W21_WriteCMD(0x30);
  EPD_W21_WriteDATA(0x08);  
	
  EPD_W21_WriteCMD(0x04);
  lcd_chkstatus();   
}

//Fast full screen refresh initialization
void EPD_init_Fast(void)
{
  epd_hardware_reset();
  EPD_W21_WriteCMD(0x4D);
  EPD_W21_WriteDATA(0x78);

  EPD_W21_WriteCMD(0x00);	//PSR
  EPD_W21_WriteDATA(0x0F);
  EPD_W21_WriteDATA(0x29);

  EPD_W21_WriteCMD(0x01);	//PWRR
  EPD_W21_WriteDATA(0x07);
  EPD_W21_WriteDATA(0x00);
  
  EPD_W21_WriteCMD(0x03);	//POFS
  EPD_W21_WriteDATA(0x10);
  EPD_W21_WriteDATA(0x54);
  EPD_W21_WriteDATA(0x44);
  
  EPD_W21_WriteCMD(0x06);	//BTST_P
	EPD_W21_WriteDATA(0x0F);
	EPD_W21_WriteDATA(0x0A);
	EPD_W21_WriteDATA(0x2F);
	EPD_W21_WriteDATA(0x25);
	EPD_W21_WriteDATA(0x22);
	EPD_W21_WriteDATA(0x2E);
	EPD_W21_WriteDATA(0x21);

  EPD_W21_WriteCMD(0x50);	//CDI
  EPD_W21_WriteDATA(0x37);
  
  EPD_W21_WriteCMD(0x61); //TRES
  EPD_W21_WriteDATA(Source_BITS/256);		// Source_BITS_H
  EPD_W21_WriteDATA(Source_BITS%256);		// Source_BITS_L
  EPD_W21_WriteDATA(Gate_BITS/256);			// Gate_BITS_H
  EPD_W21_WriteDATA(Gate_BITS%256); 		// Gate_BITS_L	
  
  EPD_W21_WriteCMD(0xE3);
  EPD_W21_WriteDATA(0x22);
	
  EPD_W21_WriteCMD(0xB6);
  EPD_W21_WriteDATA(0x6F);
  
  EPD_W21_WriteCMD(0xB4);
  EPD_W21_WriteDATA(0xD0);

  EPD_W21_WriteCMD(0xE9);
  EPD_W21_WriteDATA(0x01); 

  EPD_W21_WriteCMD(0x30);
  EPD_W21_WriteDATA(0x08);  
	
  EPD_W21_WriteCMD(0x04);
  lcd_chkstatus();   
	
	EPD_W21_WriteCMD(0xE0);
	EPD_W21_WriteDATA(0x02);
		
	EPD_W21_WriteCMD(0xE6);
	EPD_W21_WriteDATA(0x5D);
	 
	EPD_W21_WriteCMD(0xA5);
	EPD_W21_WriteDATA(0x00);
	lcd_chkstatus(); 

	
}
void EPD_sleep(void)
{   
	EPD_W21_WriteCMD(0X02);  	//power off
	EPD_W21_WriteDATA(0x00);
	lcd_chkstatus();          //waiting for the electronic paper IC to release the idle signal   
 
	EPD_W21_WriteCMD(0X07);  	//deep sleep
	EPD_W21_WriteDATA(0xA5);
}
void EPD_refresh(void)
{
  Serial.print("[DBG] before 0x12: BUSY=");
  Serial.print(digitalRead(14));
  Serial.println();
  EPD_W21_WriteCMD(0x12); //Display Update Control
  EPD_W21_WriteDATA(0x00);
  delay(10);  // Allow the display IC time to respond
  Serial.print("[DBG] after  0x12: BUSY=");
  Serial.print(digitalRead(14));
  Serial.println();
  // A full refresh actually takes ~16-25 seconds (depending on panel variation and temperature).
  // Exit early when BUSY indicates completion; cap wait at 30 seconds to prevent hanging on a slow panel.
  lcd_chkstatus_max(30000);
}




void Display_All_Black(void)
{
  unsigned long i; 

  EPD_W21_WriteCMD(0x10);
  {
    for(i=0;i<ALLSCREEN_BYTES;i++)
    {
      EPD_W21_WriteDATA(0x00);
    }
  } 
  EPD_refresh();  
  
}

void Display_All_White(void)
{
  unsigned long i;
 
  EPD_W21_WriteCMD(0x10);
  {
    for(i=0;i<ALLSCREEN_BYTES;i++)
    {
      EPD_W21_WriteDATA(0x55);
    }
  } 
   EPD_refresh(); 
}

void Display_All_Yellow(void)
{
  unsigned long i;
 
  EPD_W21_WriteCMD(0x10);
  {
    for(i=0;i<ALLSCREEN_BYTES;i++)
    {
      EPD_W21_WriteDATA(0xaa);
    }
  }
   EPD_refresh(); 
}


void Display_All_Red(void)
{
  unsigned long i;
 
  EPD_W21_WriteCMD(0x10);
  {
    for(i=0;i<ALLSCREEN_BYTES;i++)
    {
      EPD_W21_WriteDATA(0xff);
    }
  } 
   EPD_refresh(); 
}



unsigned char Color_get(unsigned char color)
{
  unsigned datas;
  switch(color)
  {
    case 0x00:
      datas=white;  
      break;    
    case 0x01:
      datas=yellow;
      break;
    case 0x02:
      datas=red;
      break;    
    case 0x03:
      datas=black;
      break;      
    default:
      break;      
  }
   return datas;
}



void PIC_display(const unsigned char* picData)
{
  unsigned int i,j;
  unsigned char temp1;
  unsigned char data_H1,data_H2,data_L1,data_L2,data;
   
  EPD_W21_WriteCMD(0x10);        
  for(i=0;i<Gate_BITS;i++)  //Source_BITS*Gate_BITS/4
  { 
    for(j=0;j<Source_BITS/4;j++)
    {   
      temp1=pgm_read_byte(&picData[i*Source_BITS/4+j]); 

      data_H1=Color_get(temp1>>6&0x03)<<6;      
      data_H2=Color_get(temp1>>4&0x03)<<4;
      data_L1=Color_get(temp1>>2&0x03)<<2;
      data_L2=Color_get(temp1&0x03);
      
      data=data_H1|data_H2|data_L1|data_L2;
      EPD_W21_WriteDATA(data);
    }
  } 
  
   //Refresh
    EPD_refresh();  
}


/***********************************************************
						end file
***********************************************************/
