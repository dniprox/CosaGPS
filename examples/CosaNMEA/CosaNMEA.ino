/*
  uart is for trace output.
  uart1 should be connected to the GPS device.
*/

#include "Cosa/Trace.hh"
#include "Cosa/IOBuffer.hh"
#include "Cosa/IOStream/Driver/UART.hh"
#include "Cosa/Power.hh"

static IOBuffer<UART::BUFFER_MAX> obuf;
static IOBuffer<UART::BUFFER_MAX> ibuf;
UART uart1(1, &ibuf, &obuf);

#include "NMEAGPS.h"

static uint32_t seconds = 0L;

static NMEAGPS gps;

//--------------------------

static void sentenceReceived()
{
#if !defined(GPS_FIX_TIME) & !defined(GPS_FIX_DATE)
  //  Date/Time not enabled, just output the interval number
  trace << seconds << ',';
#endif

  trace << gps.fix();

#if defined(NMEAGPS_PARSE_SATELLITES)
  if (gps.fix().valid.satellites) {
    trace << ',' << '[';

    uint8_t i_max = gps.fix().satellites;
    if (i_max > NMEAGPS::MAX_SATELLITES)
      i_max = NMEAGPS::MAX_SATELLITES;

    for (uint8_t i=0; i < i_max; i++) {
      trace << gps.satellites[i].id;
#if defined(NMEAGPS_PARSE_SATELLITE_INFO)
      trace << ' ' << 
        gps.satellites[i].elevation << '/' << gps.satellites[i].azimuth;
      trace << '@';
      if (gps.satellites[i].tracked)
        trace << gps.satellites[i].snr;
      else
        trace << '-';
#endif
      trace << ',';
    }
    trace << ']';
  }
#endif

  trace << '\n';

} // sentenceReceived

//--------------------------

void setup()
{
  // Start the normal trace output
  uart.begin(9600);
  trace.begin(&uart, PSTR("CosaNMEAGPS: started"));
  trace << PSTR("fix object size = ") << sizeof(gps.fix()) << endl;
  trace << PSTR("gps object size = ") << sizeof(gps) << endl;
  
  // Start the UART for the GPS device
  uart1.begin(9600);
}

//--------------------------

void loop()
{
  while (uart1.available())
    if (gps.decode( uart1.getchar() ) == NMEAGPS::DECODE_COMPLETED) {
//      trace << (uint8_t) gps.nmeaMessage << ' ';

      if (gps.nmeaMessage == NMEAGPS::NMEA_RMC) {
        sentenceReceived();

        //  Use received GPRMC sentence as a pulse
        seconds++;
      }
    }

  Power::sleep();
}