/**
******************************************************************************
* @file    BootloaderEntrance.c 
* @author  William Xu
* @version V2.0.0
* @date    05-Oct-2014
* @brief   MICO bootloader main entrance.
******************************************************************************
*
*  The MIT License
*  Copyright (c) 2014 MXCHIP Inc.
*
*  Permission is hereby granted, free of charge, to any person obtaining a copy 
*  of this software and associated documentation files (the "Software"), to deal
*  in the Software without restriction, including without limitation the rights 
*  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
*  copies of the Software, and to permit persons to whom the Software is furnished
*  to do so, subject to the following conditions:
*
*  The above copyright notice and this permission notice shall be included in
*  all copies or substantial portions of the Software.
*
*  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
*  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
*  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
*  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
*  WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR 
*  IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
******************************************************************************
*/ 


#include "mico.h"
#include "platform.h"
#include "platformInternal.h"
#include "platform_config.h"
#include "bootloader.h"

#define boot_log(M, ...) custom_log("BOOT", M, ##__VA_ARGS__)
#define boot_log_trace() custom_log_trace("BOOT")

extern void Main_Menu(void);
//extern OSStatus update(void);

//#define SIZE_OPTIMIZE

#ifdef SIZE_OPTIMIZE
char menu[] =
"\r\n"
"WiFiMCO bootloader for %s, %s, HARDWARE_REVISION: %s\r\n"
"0:BOOTUPDATE,"
"1:FWUPDATE,"
"2:DRIVERUPDAT,"
"3:PARAUPDATE,"
"4:FLASHUPDATE,"
"5:MEMORYMAP,"
"6:BOOT,"
"7:REBOOT";
#else
char menu[] =
"\r\n"
"MICO bootloader for %s, %s\r\nHARDWARE_REVISION: %s\r\n"
"+ command ------------------------+ function -------------+\r\n"
"| 0:BOOTUPDATE    <-r>            | Update bootloader     |\r\n"
"| 1:FWUPDATE      <-r>            | Update application    |\r\n"
"| 2:DRIVERUPDATE  <-r>            | Update RF driver      |\r\n"
"| 3:PARAUPDATE    <-r> <-e>       | Update MICO settings  |\r\n"
"| 4:FLASHUPDATE                   |                       |\r\n"
"|   <-dev device|-i|-s> <-e> <-r> |                       |\r\n"
"|   <-start addr><-end addr>      | Update flash content  |\r\n"
"| 5:MEMORYMAP                     | List flash memory map |\r\n"
"| 6:BOOT                          | Excute application    |\r\n"
"| 7:REBOOT                        | Reboot                |\r\n"
"| 8:ELUAFLASH                     | Erase Lua SPIFlash FS |\r\n"
"+---------------------------------+-----------------------+\r\n"
"|    (C) COPYRIGHT 2015 MXCHIP Corporation  By William Xu |\r\n"
"|    Modified by LoBo 12/2015                             |\r\n"
"+---------------------------------------------------------+\r\n"
"| Notes: (use ymodem protocol for file upload/download)   |\r\n"
"| -dev   flash device number  (0=internal, 1=spi)         |\r\n"
"| -i     Internal flash       -s   SPI flash              |\r\n"
"| -e     Erase only           -r   Read from flash        |\r\n"
"| -start flash start address  -end flash end address      |\r\n"
"| Example:                                                |\r\n"
"|   Input \"4 -dev 0 -start 0x0800c000 -end 0x0807ffff\"    |\r\n"
"|      or \"4 -i -start 0x0800c000 -end 0x0807ffff\"        |\r\n"
"|      or \"1\"                                             |\r\n"
"|   to update application in embedded flash               |\r\n"
"+---------------------------------------------------------+\r\n";
#endif

#ifdef MICO_ENABLE_STDIO_TO_BOOT
extern int stdio_break_in(void);
#endif

static void enable_protection( void )
{
  mico_partition_t i;
  mico_logic_partition_t *partition;

  for( i = MICO_PARTITION_BOOTLOADER; i < MICO_PARTITION_MAX; i++ ){
    partition = MicoFlashGetInfo( i );
    if( PAR_OPT_WRITE_DIS == ( partition->partition_options & PAR_OPT_WRITE_MASK )  )
      MicoFlashEnableSecurity( i, 0x0, MicoFlashGetInfo(i)->partition_length );
  }
}

WEAK bool MicoShouldEnterBootloader( void )
{
  return false;
}

/*
WEAK bool MicoShouldEnterMFGMode( void )
{
  return false;
}

WEAK bool MicoShouldEnterATEMode( void )
{
  return false;
}
*/
void bootloader_start_app( uint32_t app_addr )
{
  enable_protection( );
  startApplication( app_addr );
}


int main(void)
{
  //mico_logic_partition_t *partition;
  
  init_clocks();
  init_memory();
  init_architecture();
  init_platform_bootloader();

  mico_set_bootload_ver();
  
  //update();

  enable_protection();

#ifdef MICO_ENABLE_STDIO_TO_BOOT
  if (stdio_break_in() == 1)
    goto BOOT;
#endif
  
  if( MicoShouldEnterBootloader() == false )
    bootloader_start_app( (MicoFlashGetInfo(MICO_PARTITION_APPLICATION))->partition_start_addr );
  /*else if( MicoShouldEnterMFGMode() == true )
    bootloader_start_app( (MicoFlashGetInfo(MICO_PARTITION_APPLICATION))->partition_start_addr );
  else if( MicoShouldEnterATEMode() ){
    partition = MicoFlashGetInfo( MICO_PARTITION_ATE );
    if (partition->partition_owner != MICO_FLASH_NONE) {
      bootloader_start_app( partition->partition_start_addr );
    }
  }*/

#ifdef MICO_ENABLE_STDIO_TO_BOOT
BOOT:
#endif
  
  printf ( menu, MODEL, Bootloader_REVISION, HARDWARE_REVISION );

  while(1){                             
    Main_Menu ();
  }
}


