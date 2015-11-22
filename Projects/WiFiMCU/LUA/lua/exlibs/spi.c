/**
 * spi.c
 */

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "lrotable.h"

#include "mico_platform.h"

#define BITS_8          8
#define BITS_16         16

extern const char wifimcu_gpio_map[];

#define NUM_GPIO 18
static int platform_gpio_exists( unsigned pin )
{
  return pin < NUM_GPIO;
}

uint8_t spiInit[3] = {0,0,0};
uint8_t spiRW[3] = {0,0,0};

/**********************/
/**** Hardware SPI ****/
/**********************/

/* SPI1 */
static mico_spi_device_t HW_SPI1 =
{
    .port        = MICO_SPI_1,
    .chip_select = MICO_GPIO_38,
    .speed       = 400000,        // 400000 - 50000000
    //.mode        = (SPI_CLOCK_RISING_EDGE | SPI_CLOCK_IDLE_HIGH | SPI_USE_DMA | SPI_MSB_FIRST),
    .mode        = (SPI_CLOCK_RISING_EDGE | SPI_CLOCK_IDLE_LOW | SPI_MSB_FIRST),
    .bits        = 8
};

/* SPI5 */
static mico_spi_device_t HW_SPI5 =
{
    .port        = MICO_SPI_5,
    .chip_select = MICO_GPIO_38,
    .speed       = 400000,        // 400000 - 50000000
    //.mode        = (SPI_CLOCK_RISING_EDGE | SPI_CLOCK_IDLE_HIGH | SPI_USE_DMA | SPI_MSB_FIRST),
    .mode        = (SPI_CLOCK_RISING_EDGE | SPI_CLOCK_IDLE_LOW | SPI_MSB_FIRST),
    .bits        = 8
};


/**********************/
/**** Software SPI ****/
/**********************/

/* SW_SPI.spiMode=0;       cpol 0 cpha 1: sck=0 rising  edge send/read data
   SW_SPI.spiMode=1;       cpol 0 cpha 0: sck=0 falling edge send/read data
   SW_SPI.spiMode=2;       cpol 1 cpha 1: sck=1 falling edge send/read data
   SW_SPI.spiMode=3;       cpol 1 cpha 0: sck=1 rising  edge send/read data */

typedef struct {
	uint8_t  spiMode;
	uint8_t  pinSCK;
	uint8_t  pinMOSI;
	uint8_t  pinMISO;
	uint8_t  pinCS;
	uint32_t speed;
} swspi_t;

static swspi_t SW_SPI =
{
  .spiMode  = 0,
  .pinSCK   = 255, // unassigned
  .pinMOSI  = 255, // unassigned
  .pinMISO  = 255, // unassigned
  .pinCS    = 255, // unassigned
  .speed = 500     // 500kHz
};

//-------------------------------------------------------------------------
// we use cycle counter for precise timing with software SPI
#define CYCLE_COUNTING_INIT() \
    do \
    { \
        /* enable DWT hardware and cycle counting */ \
        CoreDebug->DEMCR = CoreDebug->DEMCR | CoreDebug_DEMCR_TRCENA_Msk; \
        /* reset a counter */ \
        DWT->CYCCNT = 0;  \
        /* enable the counter */ \
        DWT->CTRL = (DWT->CTRL | DWT_CTRL_CYCCNTENA_Msk) ; \
    } \
    while(0)
//-------------------------------------------------------------------------

//--------------------
void spi_delay(void) {
  uint32_t cycles = 0;

  CYCLE_COUNTING_INIT();
  while (cycles < SW_SPI.speed) {
    cycles = DWT->CYCCNT;
  }
}


//id:     0 for software spi; 1 for hw spi1; 2 for hw spi5
//cpnfig: lua table: {mode,sck,mosi,[miso],[rw],[speed]}
//spi.setup(id, config)
//==================================
static int spi_setup( lua_State* L )
{
  uint8_t err = 0;
  
  uint8_t id = luaL_checkinteger( L, 1 );
  if ((id !=0) && (id !=1) && (id !=2)) {
    l_message( NULL, "id should assigend 0,1 or 2" );
    lua_pushinteger( L, -1 );
    return 1;
  }
    
  if (!lua_istable(L, 2)) {
    l_message( NULL, "table arg needed" );
    lua_pushinteger( L, -2 );
    return 1;
  }

  spiInit[id] = 0;
  spiRW[id] = 0;
  // --- spi Mode --------------------------------------------------------------
  lua_getfield(L, 2, "mode");
  if (!lua_isnil(L, -1)){  /* found? */
    if( lua_isstring(L, -1) )   // deal with the string
    {
      if (id==0) {
        SW_SPI.spiMode = luaL_checkinteger( L, -1 );
        if (SW_SPI.spiMode > 3) SW_SPI.spiMode = 0;
      }
      else {
        uint8_t md = luaL_checkinteger( L, -1 );
        if (md > 3) SW_SPI.spiMode = 0;
        if (md == 0) md = (SPI_CLOCK_RISING_EDGE | SPI_CLOCK_IDLE_LOW | SPI_MSB_FIRST);
        else if (md == 1) md = (SPI_CLOCK_FALLING_EDGE | SPI_CLOCK_IDLE_LOW | SPI_MSB_FIRST);
        else if (md == 2) md = (SPI_CLOCK_FALLING_EDGE | SPI_CLOCK_IDLE_HIGH | SPI_MSB_FIRST);
        else md = (SPI_CLOCK_RISING_EDGE | SPI_CLOCK_IDLE_HIGH | SPI_MSB_FIRST);
        if (id==2) HW_SPI5.mode = md;
        else HW_SPI1.mode = md;
      }
    } 
    else {
      l_message( NULL, "wrong arg type:mode" );
      lua_pushinteger( L, -3 );
      return 1;
    }
  }
  else {
    l_message( NULL, "arg: mode needed" );
    lua_pushinteger( L, -4 );
    return 1;
  }
  
  // --- SPI speed (in Khz) ----------------------------------------------------
  lua_getfield(L, 2, "speed");
  if (!lua_isnil(L, -1)){  /* found? */
    if( lua_isstring(L, -1) )   // deal with the string
    {
      if (id==0) {
        SW_SPI.speed = luaL_checkinteger( L, -1 );
        if ((SW_SPI.speed > 5000) || (SW_SPI.speed < 100)) {
          l_message( NULL, "wrong arg value: 100kHz<=speed<=5000kHz" );
          lua_pushinteger( L, -5 );
          return 1;
        }
        SW_SPI.speed = 50000 / SW_SPI.speed;
      }
      else {
        uint32_t sp = luaL_checkinteger( L, -1 );
        if ((sp > 50000) || (SW_SPI.speed < 400)) {
          l_message( NULL, "wrong arg value: 400kHz<=speed<=50000kHz" );
          lua_pushinteger( L, -5 );
          return 1;
        }
        sp = sp * 1000;
        if (id==2) HW_SPI5.speed = sp;
        else HW_SPI1.speed = sp;
      }
    } 
    else {
      l_message( NULL, "wrong arg type:speed" );
      lua_pushinteger( L, -6 );
      return 1;
    }
  }
  else {
    if (id==0) SW_SPI.speed = 500;
    else if (id==2) HW_SPI5.speed = 1000000;
    else HW_SPI1.speed = 1000000;
  }
  
  // --- CS pin ----------------------------------------------------------------
  lua_getfield(L, 2, "cs");
  if (!lua_isnil(L, -1)){  /* found? */
    if( lua_isstring(L, -1) )   // deal with the string
    {
      unsigned pin = luaL_checkinteger( L, -1 );
      MOD_CHECK_ID( gpio, pin );
      if (id==0) {
        SW_SPI.pinCS = 255;
        SW_SPI.pinCS = wifimcu_gpio_map[pin];
        MicoGpioFinalize((mico_gpio_t)SW_SPI.pinCS);
        MicoGpioInitialize((mico_gpio_t)SW_SPI.pinCS,(mico_gpio_config_t)OUTPUT_PUSH_PULL);
      }
      else if (id == 2) {
        HW_SPI5.chip_select = (mico_gpio_t)wifimcu_gpio_map[pin];
      }
      else {
        HW_SPI1.chip_select = (mico_gpio_t)wifimcu_gpio_map[pin];
      }
        
    } 
    else {
      l_message( NULL, "wrong arg type:cs" );
      lua_pushinteger( L, -7 );
      return 1;
    }
  }
  else {
    l_message( NULL, "arg: cs needed" );
    lua_pushinteger( L, -8 );
    return 1;
  }
 
  // --- RW --------------------------------------------------------------------
  lua_getfield(L, 2, "rw");
  if (!lua_isnil(L, -1)){  /* found? */
    if( lua_isstring(L, -1) )   // deal with the string
    {
        unsigned pin = luaL_checkinteger( L, -1 );
        if (pin == 1) spiRW[id] = 1;
    }
    else {
      l_message( NULL, "wrong arg type:dc" );
      lua_pushinteger( L, -9 );
      return 1;
    }
  }

  // --- Only for software SPI -------------------------------------------------
  if (id==0) {
    // --- SW_SPI.pinSCK --------------------------------------------------------------
    lua_getfield(L, 2, "sck");
    if (!lua_isnil(L, -1)){  /* found? */
      if( lua_isstring(L, -1) )   // deal with the string
      {
          SW_SPI.pinSCK = 255;
          unsigned pin = luaL_checkinteger( L, -1 );
          MOD_CHECK_ID( gpio, pin );
          SW_SPI.pinSCK = wifimcu_gpio_map[pin];
          MicoGpioFinalize((mico_gpio_t)SW_SPI.pinSCK);
          MicoGpioInitialize((mico_gpio_t)SW_SPI.pinSCK,(mico_gpio_config_t)OUTPUT_PUSH_PULL);
      }
      else {
        l_message( NULL, "wrong arg type:sck" );
        lua_pushinteger( L, -10 );
        return 1;
      }
    }
    else {
      l_message( NULL, "arg: sck needed" );
      lua_pushinteger( L, -11 );
      return 1;
    }
    
    // --- SW_SPI.pinMOSI -------------------------------------------------------------
    lua_getfield(L, 2, "mosi");
    if (!lua_isnil(L, -1)){  /* found? */
      if( lua_isstring(L, -1) )   // deal with the string
      {
          SW_SPI.pinMOSI = 255;
          unsigned pin = luaL_checkinteger( L, -1 );
          MOD_CHECK_ID( gpio, pin );
          SW_SPI.pinMOSI = wifimcu_gpio_map[pin];
          MicoGpioFinalize((mico_gpio_t)SW_SPI.pinMOSI);
          MicoGpioInitialize((mico_gpio_t)SW_SPI.pinMOSI,(mico_gpio_config_t)OUTPUT_PUSH_PULL);
      }
      else {
        l_message( NULL, "wrong arg type:mosi" );
        lua_pushinteger( L, -12 );
        return 1;
      }
    }
    else {
      l_message( NULL, "arg: mosi needed" );
      lua_pushinteger( L, -13 );
      return 1;
    }
    
    // --- SW_SPI.pinMISO -------------------------------------------------------------
    lua_getfield(L, 2, "miso");
    if (!lua_isnil(L, -1)){  /* found? */
      if( lua_isstring(L, -1) )   // deal with the string
      {
          SW_SPI.pinMISO = 255;
          unsigned pin = luaL_checkinteger( L, -1 );
          MOD_CHECK_ID( gpio, pin );
          SW_SPI.pinMISO = wifimcu_gpio_map[pin];
          MicoGpioFinalize((mico_gpio_t)SW_SPI.pinMISO); 
          MicoGpioInitialize((mico_gpio_t)SW_SPI.pinMISO,(mico_gpio_config_t)INPUT_PULL_UP);
      }
      else {
        l_message( NULL, "wrong arg type:miso" );
        lua_pushinteger( L, -14 );
        return 1;
      }
    }
    else SW_SPI.pinMISO = 255;
  }
  
  if (id == 0) {
    if(SW_SPI.spiMode==2 || SW_SPI.spiMode==3) MicoGpioOutputHigh( (mico_gpio_t)SW_SPI.pinSCK );
    else MicoGpioOutputLow( (mico_gpio_t)SW_SPI.pinSCK );
    MicoGpioOutputHigh( (mico_gpio_t)SW_SPI.pinCS );  // CS=high
  }
  else if (id==2) {
    err = MicoSpiInitialize(&HW_SPI5);
    if (err != kNoErr) {
      l_message( NULL, "error initializing hw SPI5" );
      lua_pushinteger( L, -15 );
      return 1;
    }
  }
  else {
    err = MicoSpiInitialize(&HW_SPI1);
    if (err != kNoErr) {
      l_message( NULL, "error initializing hw SPI1" );
      lua_pushinteger( L, -15 );
      return 1;
    }
  }
  
  spiInit[id] = 1;
  lua_pushinteger( L, 0 );
  return 1;
}

// Writes a byte to the SPI
//-------------------------------------------------------------------------------
uint16_t hw_spi_write(uint8_t id, uint8_t databits, uint8_t data, uint16_t count)
{  
  uint16_t rxdata=0x0000;
  mico_spi_message_segment_t hwspi_msg = { &data, NULL, (unsigned long)count };
  
  if (spiRW[id]) hwspi_msg.rx_buffer = &rxdata;
  if (id==2) {
    HW_SPI5.bits = databits;
    MicoSpiTransfer( &HW_SPI5, &hwspi_msg, 1 );
  }
  else if (id==1) {
    HW_SPI1.bits = databits;
    MicoSpiTransfer( &HW_SPI1, &hwspi_msg, 1 );
  }
  
  if (databits==BITS_8) rxdata= rxdata & 0x00FF;
  return rxdata;
} 

/* SW_SPI.spiMode=0;       cpol 0 cpha 1: sck=0 rising  edge send/read data
   SW_SPI.spiMode=1;       cpol 0 cpha 0: sck=0 falling edge send/read data
   SW_SPI.spiMode=2;       cpol 1 cpha 1: sck=1 falling edge send/read data
   SW_SPI.spiMode=3;       cpol 1 cpha 0: sck=1 rising  edge send/read data */
//---------------------------------------------------------
static uint16_t sw_spi_write(uint8_t databits,uint8_t data)
{
  uint8_t i=0;
  uint16_t rxdata=0x0000;
  
  // set clk inactive state
  if (SW_SPI.spiMode > 1) MicoGpioOutputHigh( (mico_gpio_t)SW_SPI.pinSCK );
  else MicoGpioOutputLow( (mico_gpio_t)SW_SPI.pinSCK );

  // activate CS
  MicoGpioOutputLow( (mico_gpio_t)SW_SPI.pinCS );
  spi_delay();

  for(i=0;i<databits;i++)  
  {
    // set clk state before write edge
    if ((SW_SPI.spiMode == 0) || (SW_SPI.spiMode == 2)) MicoGpioOutputLow( (mico_gpio_t)SW_SPI.pinSCK );
    else MicoGpioOutputHigh( (mico_gpio_t)SW_SPI.pinSCK );
    
    // set data bit -> MOSI
    if(databits==8)
    {
      if((data & 0x80)==0x80) MicoGpioOutputHigh( (mico_gpio_t)SW_SPI.pinMOSI );  
      else MicoGpioOutputLow( (mico_gpio_t)SW_SPI.pinMOSI );
    }
    else
    {
      if((data & 0x8000)==0x8000) MicoGpioOutputHigh( (mico_gpio_t)SW_SPI.pinMOSI );  
      else MicoGpioOutputLow( (mico_gpio_t)SW_SPI.pinMOSI );
    }
    spi_delay();

    // set clk write edge
    if ((SW_SPI.spiMode == 0) || (SW_SPI.spiMode == 2)) MicoGpioOutputHigh( (mico_gpio_t)SW_SPI.pinSCK );
    else MicoGpioOutputLow( (mico_gpio_t)SW_SPI.pinSCK );
    spi_delay();

    if ((spiRW[0]) && (SW_SPI.pinMISO != 255)) {
      // get data bit <- MISO
      data=(data<<1);
      if(MicoGpioInputGet((mico_gpio_t)SW_SPI.pinMISO)) data |= 1;
    }

    data=(data<<1);  // next bit
  }
  
  // set clk inactive state
  if (SW_SPI.spiMode > 1) MicoGpioOutputHigh( (mico_gpio_t)SW_SPI.pinSCK );
  else MicoGpioOutputLow( (mico_gpio_t)SW_SPI.pinSCK );
  
  // deactivate CS
  MicoGpioOutputHigh( (mico_gpio_t)SW_SPI.pinCS );

  if (databits==BITS_8) rxdata= rxdata & 0x00FF;
  return rxdata;
}

// Reads a byte/word from SPI
//-----------------------------------------------------------------------
static uint16_t hw_spi_read(uint8_t id, uint8_t databits, uint16_t count)
{  
  OSStatus err = kNoErr;
  uint16_t data = 0x0000;
  
  mico_spi_message_segment_t hwspi_msg = { NULL, &data, (unsigned long)count };

  if (id==2) {
    HW_SPI5.bits = databits;
    err = MicoSpiTransfer( &HW_SPI5, &hwspi_msg, 1 );
  }
  else if (id==1) {
    HW_SPI1.bits = databits;
    err = MicoSpiTransfer( &HW_SPI1, &hwspi_msg, 1 );
  }
  if (err) data = 0;
  if (databits==BITS_8) data=data & 0xFF;
  return data;
} 

//---------------------------------------------
static uint16_t sw_spi_read(uint8_t databits)
{
  uint16_t data=0x0000;
  
  uint8_t i=0;
  
  // set clk inactive state
  if (SW_SPI.spiMode > 1) MicoGpioOutputHigh( (mico_gpio_t)SW_SPI.pinSCK );
  else MicoGpioOutputLow( (mico_gpio_t)SW_SPI.pinSCK );

  // activate CS
  MicoGpioOutputLow( (mico_gpio_t)SW_SPI.pinCS );
  spi_delay();

  for(i=0;i<databits;i++)  
  {
    // set clk state before read edge
    if ((SW_SPI.spiMode == 0) || (SW_SPI.spiMode == 2)) MicoGpioOutputLow( (mico_gpio_t)SW_SPI.pinSCK );
    else MicoGpioOutputHigh( (mico_gpio_t)SW_SPI.pinSCK );
    spi_delay();

    // set clk read edge
    if ((SW_SPI.spiMode == 0) || (SW_SPI.spiMode == 2)) MicoGpioOutputHigh( (mico_gpio_t)SW_SPI.pinSCK );
    else MicoGpioOutputLow( (mico_gpio_t)SW_SPI.pinSCK );
    spi_delay();

    // get data bit <- MISO
    data=(data<<1);
    if(MicoGpioInputGet((mico_gpio_t)SW_SPI.pinMISO)) data |= 1;
  }
  
  // set clk inactive state
  if (SW_SPI.spiMode > 1) MicoGpioOutputHigh( (mico_gpio_t)SW_SPI.pinSCK );
  else MicoGpioOutputLow( (mico_gpio_t)SW_SPI.pinSCK );
  
  // deactivate CS
  MicoGpioOutputHigh( (mico_gpio_t)SW_SPI.pinCS );

  if (databits==BITS_8) data=data & 0x00FF;
  return data;
}

//-------------------------------------------------------------------
uint16_t _spi_write(uint8_t id, uint8_t databits,uint8_t data)
{
  if (id == 0) return sw_spi_write(databits, data);
  else return hw_spi_write(id,databits,data,1);
}

//-----------------------------------------------------
static uint16_t _spi_read(uint8_t id, uint8_t databits)
{
  if (id == 0) return sw_spi_read(databits);
  else return hw_spi_read(id,databits,1);
}

//spi.write(id,databits,data1,[data2],...)
//==================================
static int spi_write( lua_State* L )
{
  uint8_t id = luaL_checkinteger( L, 1 );
  if ((id !=0) && (id !=1) && (id !=2)) {
    l_message( NULL, "id should assigend 0,1 or 2" );
    lua_pushinteger( L, -1 );
    return 1;
  }
  if (!spiInit[id]) {
    l_message( NULL, "spi not yet initialized" );
    lua_pushinteger( L, -2 );
    return 1;
  }

  uint8_t databits = luaL_checkinteger( L, 2 );
  if (databits !=BITS_8 && databits !=BITS_16 ) {
    l_message( NULL, "databits should BITS_8 or BITS_16" );
    lua_pushinteger( L, -3 );
    return 1;
  }

  if ( lua_gettop( L ) < 3 ) {
    l_message( NULL, "wrong arg type" );
    lua_pushinteger( L, -4 );
    return 1;
  }
  
  size_t datalen=0, i=0;
  int numdata=0;
  uint32_t wrote = 0;
  unsigned argn=0;
  uint16_t rxdata = 0;
  
  wrote = 0;
  for( argn = 3; argn <= lua_gettop( L ); argn++ )
  {
    if( lua_type( L, argn ) == LUA_TNUMBER )
    { // write integer
      numdata = ( int )luaL_checkinteger( L, argn );
      if( databits==BITS_8 &&( numdata < 0 || numdata > 255 ))
        return luaL_error( L, "wrong arg range" );
      if( databits==BITS_16 &&( numdata < 0 || numdata > 65535 ))
        return luaL_error( L, "wrong arg range" );      
      rxdata = _spi_write(id, databits, numdata);
      wrote += 1;
    }
    else if( lua_istable( L, argn ) )
    { // write table of integers
      datalen = lua_objlen( L, argn );
      for( i = 0; i < datalen; i++ )
      {
        lua_rawgeti( L, argn, i + 1 );
        numdata = ( int )luaL_checkinteger( L, -1 );
        lua_pop( L, 1 );
        if( databits==BITS_8 &&( numdata < 0 || numdata > 255 ))
          return luaL_error( L, "wrong arg range" );
        if( databits==BITS_16 &&( numdata < 0 || numdata > 65535 ))
          return luaL_error( L, "wrong arg range" );
        _spi_write(id, databits, numdata);
        wrote += 1;
      }
      if( i < datalen ) return luaL_error( L, "table error" );
    }
    else
    { // write string
      const char *pdata = luaL_checklstring( L, argn, &datalen );
      if (id==0) {
        for( i = 0; i < datalen; i++ )
        {
          _spi_write(id, BITS_8, (uint8_t)pdata[i]);
          wrote += 1;
        }
        if( i < datalen ) return luaL_error( L, "string error" );
      }
      else {
        hw_spi_write(id, BITS_8, (uint8_t)pdata[i], datalen);
        wrote += datalen;
      }
    }
  }
  if (wrote > 0) wrote--;
  if (spiRW[id]) lua_pushinteger( L, rxdata );
  else lua_pushinteger( L, wrote );
  
  return 1;
}

//spi.write(id,databits,data,n)
//========================================
static int spi_repeatwrite( lua_State* L )
{
  uint8_t id = luaL_checkinteger( L, 1 );
  if ((id !=0) && (id !=1) && (id !=2)) {
    l_message( NULL, "id should assigend 0,1 or 2" );
    lua_pushinteger( L, -1 );
    return 1;
  }
  if (!spiInit[id]) {
    l_message( NULL, "spi not yet initialized" );
    lua_pushinteger( L, -2 );
    return 1;
  }

  uint8_t databits = luaL_checkinteger( L, 2 );
  if (databits !=BITS_8 && databits !=BITS_16 ) {
    l_message( NULL, "databits should BITS_8 or BITS_16" );
    lua_pushinteger( L, -3 );
    return 1;
  }

  if ( lua_gettop( L ) < 4 ) {
    l_message( NULL, "wrong arg type" );
    lua_pushinteger( L, -4 );
    return 1;
  }
  
  uint16_t i = 0;
  uint16_t wrote = 0;

  uint16_t data = luaL_checkinteger( L, 3 );
  uint16_t count = luaL_checkinteger( L, 4 );
  
  if( (databits==BITS_8) && ( data > 255 )) data=data & 0xFF;

  uint8_t brw = spiRW[id];
  spiRW[id] = 0;
  wrote = 0;
  
  for (i=0; i<count; i++) {
    _spi_write(id, databits, data);
    wrote += 1;
  }
  spiRW[id] = brw;

  lua_pushinteger( L, wrote );
  
  return 1;
}

//spi.read(id,databits,n)
//=================================
static int spi_read( lua_State* L )
{
  unsigned id = luaL_checkinteger( L, 1 );

  if ((id !=0) && (id !=1) && (id !=2)) {
    l_message( NULL, "id should assigend 0,1 or 2" );
    lua_pushinteger( L, -1 );
    return 1;
  }
  if (!spiInit[id]) {
    l_message( NULL, "spi not yet initialized" );
    lua_pushinteger( L, -2 );
    return 1;
  }
      
  uint8_t databits = luaL_checkinteger( L, 2 );
  if (databits !=BITS_8 && databits !=BITS_16 ) {
    l_message( NULL, "databits should BITS_8 or BITS_16" );
    lua_pushinteger( L, -3 );
    return 1;
  }
  
  uint32_t size = ( uint32_t )luaL_checkinteger( L, 3);
  if ( size == 0 ) {
    l_message( NULL, "nothing to read" );;
    lua_pushinteger( L, -4 );
    return 1;
  }

  if ((id==0) && (SW_SPI.pinMISO==255)) {
    l_message( NULL, "MISO not yet initialized" );
    lua_pushinteger( L, -5 );
    return 1;
  }
  
  int i=0;
  uint16_t data = 0;

  if (size == 1) {
    data = _spi_read(id, databits);
    lua_pushinteger( L, data );
  }
  else {
    lua_newtable(L);
    if (id == 0) {
      for( i = 0; i < size; i ++ )
      {
        data = _spi_read(id, databits);
        lua_pushinteger( L, data );
        lua_rawseti(L,-2,i + 1);
      }
    }
  }
  return 1;
}

//spi.readbytes(id,n)
//======================================
static int spi_readbytes( lua_State* L )
{
  unsigned id = luaL_checkinteger( L, 1 );
  if ((id !=0) && (id !=1) && (id !=2)) {
    l_message( NULL, "id should assigend 0,1 or 2" );
    lua_pushinteger( L, -1 );
    return 1;
  }
  if (!spiInit[id]) {
    l_message( NULL, "spi not yet initialized" );
    lua_pushinteger( L, -2 );
    return 1;
  }
      
  if ((id==0) && (SW_SPI.pinMISO==255)) {
    l_message( NULL, "MISO not yet initialized" );
    lua_pushinteger( L, -3 );
    return 1;
  }

  uint32_t size = ( uint32_t )luaL_checkinteger( L, 2);
  if ( size == 0 ) {
    l_message( NULL, "nothing to read" );
    lua_pushinteger( L, -4 );
    return 1;
  }
  if ( size > 512 ) {
    l_message( NULL, "max 512 bytes can be read" );
    lua_pushinteger( L, -5 );
    return 1;
  }
  
  int i=0;
  uint8_t data;

  lua_newtable(L);
  if (id == 0) {
    for( i = 0; i < size; i ++ )
    {
      data = _spi_read(id, BITS_8);
      lua_pushinteger( L, data );
      lua_rawseti(L,-2,i + 1);
    }
  }
  else {
    uint8_t rxbuffer[512];

    mico_spi_message_segment_t hwspi_msg = { NULL, &rxbuffer, (unsigned long)size };
    if (id==2) data = MicoSpiTransfer( &HW_SPI5, &hwspi_msg, 1 );
    else data = MicoSpiTransfer( &HW_SPI1, &hwspi_msg, 1 );
    if (data == kNoErr) {
      for( i = 0; i < size; i ++ )
      {
        lua_pushinteger( L, rxbuffer[i] );
        lua_rawseti(L,-2,i + 1);
      }
    }
  }

  return 1;
}

//===================================
static int spi_deinit( lua_State* L )
{
  unsigned id = luaL_checkinteger( L, 1 );
  if ((id !=0) && (id !=1) && (id !=2)) {
    l_message( NULL, "id should assigend 0,1 or 2" );
    lua_pushinteger( L, -1 );
    return 1;
  }
  if (!spiInit[id]) {
    l_message( NULL, "spi not yet initialized" );
    lua_pushinteger( L, -2 );
    return 1;
  }

  if (id == 0) {
    MicoGpioFinalize((mico_gpio_t)SW_SPI.pinCS);
    MicoGpioInitialize((mico_gpio_t)SW_SPI.pinCS,(mico_gpio_config_t)INPUT_PULL_UP);
    MicoGpioFinalize((mico_gpio_t)SW_SPI.pinMOSI);
    MicoGpioInitialize((mico_gpio_t)SW_SPI.pinMOSI,(mico_gpio_config_t)INPUT_PULL_UP);
    MicoGpioFinalize((mico_gpio_t)SW_SPI.pinSCK);
    MicoGpioInitialize((mico_gpio_t)SW_SPI.pinSCK,(mico_gpio_config_t)INPUT_PULL_UP);
  }
  else if (id == 1) {
    MicoSpiFinalize(&HW_SPI1);
    MicoGpioFinalize((mico_gpio_t)HW_SPI1.chip_select);
    MicoGpioInitialize((mico_gpio_t)HW_SPI1.chip_select,(mico_gpio_config_t)INPUT_PULL_UP);
  }
  else if (id == 1) {
    MicoSpiFinalize(&HW_SPI5);
    MicoGpioFinalize((mico_gpio_t)HW_SPI5.chip_select);
    MicoGpioInitialize((mico_gpio_t)HW_SPI5.chip_select,(mico_gpio_config_t)INPUT_PULL_UP);
  }

  spiInit[id] = 0;

  lua_pushinteger( L, 0 );
  return 1;
}

#define MIN_OPT_LEVEL   2
#include "lrodefs.h"
const LUA_REG_TYPE spi_map[] =
{
  { LSTRKEY( "setup" ), LFUNCVAL( spi_setup )},
  { LSTRKEY( "write" ), LFUNCVAL( spi_write )},
  { LSTRKEY( "repeatwrite" ), LFUNCVAL( spi_repeatwrite )},
  { LSTRKEY( "read" ), LFUNCVAL( spi_read )},
  { LSTRKEY( "readbytes" ), LFUNCVAL( spi_readbytes )},
  { LSTRKEY( "deinit" ), LFUNCVAL( spi_deinit )},
#if LUA_OPTIMIZE_MEMORY > 0
  { LSTRKEY( "BITS_8" ), LNUMVAL( BITS_8 ) },
  { LSTRKEY( "BITS_16" ), LNUMVAL( BITS_16 ) },
#endif    
  {LNILKEY, LNILVAL}
};

LUALIB_API int luaopen_spi(lua_State *L)
{

#if LUA_OPTIMIZE_MEMORY > 0
  return 0;
#else
  luaL_register( L, EXLIB_SPI, spi_map );
  MOD_REG_NUMBER( L, "BITS_8", BITS_8);
  MOD_REG_NUMBER( L, "BITS_16", BITS_16);
  return 1;
#endif
}
