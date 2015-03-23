// Compile and run with:
// arduino --upload --board arduino:avr:uno --port /dev/ttyACM0 randomPoints.ino

// Need a ring buffer of flares (q.v.): array, index, modulo arithmetic.  Initialize with invalid points.
// 
// Loop: sleep, 

#include "Adafruit_WS2801.h"
#include "SPI.h" // Comment out this line if using Trinket or Gemma
#ifdef __AVR_ATtiny85__
#include <avr/power.h>
#endif

// ---------------------------------------------------------------------------------------------------------------------
//  Types, Globals
// ---------------------------------------------------------------------------------------------------------------------

#define GRID_SIZE 5

// Max. flare age
#define MAX_AGE 80

#define FLARE_CHANCE_DENOMINATOR 70

// Max. number of flares.
#define MAX_FLARES 10

// Milliseconds
#define DELAY_TIME 20

#define ZERO_BRIGHTNESS 0.00195

struct point
{
   int x,y;
};

struct rgbTriple
{
   int r,g,b;
};

struct flare
{
   bool isValid;                // True iff this is valid data.
   struct point locn;           // Where the flare is.
   int hue;                     // The flare's color.
   int age;                     // How old the flare is, in generations.  0 is youngest, higher numbers are older.
};


uint8_t dataPin  = 2;    // Yellow wire on Adafruit Pixels
uint8_t clockPin = 3;    // Green wire on Adafruit Pixels

Adafruit_WS2801 strip = Adafruit_WS2801(25, dataPin, clockPin);

int flareCount = 0;             // Number of live flares
int nextFlareIx = 0;            // Index in flare array of next flare to be created.

struct flare flares[MAX_FLARES];

/*
  Adjust hue to make less blue and more red.  The idea is to transform hue to "adjusted hues" such that angles near 0
  (red) are adjusted closer to 0 (which makes look like there's more red in the hue circle), and angles near 240 (blue)
  are adjusted such that they are "spread out" from 240, making it appear that there is LESS blue in the circle.

  Maxima inputs are as follows.  Obviously, you can fiddle with the "radius" and "factor" (they control the amount of
  adjustment) and the fixed points that are input into the spline.  Those fixed points are essentially the places where
  no adjustment will take place.

  (%i1) radius:30;factor:1.5;
  (%i2) pts:[
          [0,0],    
          [radius, radius/factor],
          [60,60],
          [120,120],
          [180,180],
          [240-radius, 240-radius*factor],
          [240,240],
          [240+radius,240+radius*factor],
          [300,300],
          [360-radius,360-radius/factor],
          [360,360]
          ];
   (%i3) load(interpol);
   (%i4) cspline(pts);
   (%i5) f(x):=''%;
   (%i6) plot2d([x,[discrete,pts],f(x)],[x,0,360],[grid2d,true],[style,lines,linespoints]);
   (%i7) makelist(floor(f(h)+0.5),h,0,359);
 */
short adjustedHue[] = { 0,0,0,1,1,1,1,2,2,2,2,3,3,3,4,4,5,5,6,6,7,7,8,9,9,10,11,12,13,14,15,16,17,18,20,21,22,24,25,27,28,30,31,33,34,36,38,39,41,43,44,46,47,49,51,52,54,55,57,59,60,61,63,64,66,67,68,70,71,72,73,74,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,94,95,96,97,98,99,100,100,101,102,103,104,105,105,106,107,108,109,110,111,112,112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127,128,130,131,132,133,134,135,136,137,139,140,141,142,143,144,146,147,148,149,150,151,152,154,155,156,157,158,159,160,161,162,163,164,165,166,167,168,169,170,171,172,172,173,174,175,176,176,177,178,178,179,179,180,181,181,181,182,182,183,183,183,184,184,185,185,185,186,186,186,187,187,188,188,189,189,190,190,191,192,193,193,194,195,196,197,198,199,200,201,203,204,205,207,208,210,211,213,214,216,217,219,221,222,224,226,227,229,231,233,235,236,238,240,242,244,245,247,249,251,253,254,256,258,260,261,263,265,266,268,269,271,272,274,275,277,278,279,280,281,282,283,284,285,286,286,287,288,288,289,289,289,290,290,291,291,291,291,292,292,292,293,293,293,294,294,295,295,296,297,297,298,299,300,301,302,303,305,306,307,309,310,312,313,315,317,318,320,322,323,325,327,328,330,332,333,335,337,338,340,341,342,344,345,346,347,348,349,350,351,352,353,353,354,355,355,356,356,357,357,357,358,358,358,358,359,359,359,359,359,360,360,360 };

// ---------------------------------------------------------------------------------------------------------------------
//  setup
// ---------------------------------------------------------------------------------------------------------------------

void setup() {
   int i;
   struct rgbTriple rgb;
   float dHue = 360.0 / 25.0;
   int h;
   int hBias;
   int x,y;
   struct point pt;
   
   strip.begin();
   strip.show();
   randomSeed( 1);
   Serial.begin( 115200);

   clearStrip();

   // // for (hBias = 0; hBias < (360 * 1); hBias += (int) (360.0 / 25 + 0.5))
   // for (hBias = 0; hBias < 25; hBias++)
   // {
   //    for (i = 0; i < 25; i++)
   //    {
   //       h = (int) (i * dHue + 0.5);
   //       rgb = hsl2rgb( h, 1.0, 0.5);
   //       strip.setPixelColor( (i + hBias) % 25, Color( rgb.r, rgb.g, rgb.b));
   //    }
   //    strip.show();
   //    delay( 2000);
   // }

   // dHue = 10;
   // clearStrip();
   // for (x = 0; x < 5; x++)
   // {
   //    for (y = 3; y < 5; y++)
   //    {
   //       pt = {x,y};
   //       h = (x-2) * dHue + 0;     // either side of red
   //       rgb = hsl2rgb( h, 1.0, 0.5);
   //       strip.setPixelColor( point2seq0( pt), Color( rgb.r, rgb.g, rgb.b));
   //    }
   //    for (y = 0; y < 2; y++)
   //    {
   //       pt = {x,y};
   //       h = (x-2) * dHue + 240;     // either side of blue
   //       rgb = hsl2rgb( h, 1.0, 0.5);
   //       strip.setPixelColor( point2seq0( pt), Color( rgb.r, rgb.g, rgb.b));
   //    }
   // }
   // strip.show();
   // delay( 20000);
   
   // clearStrip();
}

// ---------------------------------------------------------------------------------------------------------------------
//  loop
// ---------------------------------------------------------------------------------------------------------------------

void loop() {
   // put your main code here, to run repeatedly:

   // Serial.print( "millis() = ");
   // Serial.println( millis());

   // Serial.print( "flareCount = ");
   // Serial.println( flareCount);

   if (flareCount < MAX_FLARES)
   {
      long r;

      // if (flareCount == 0)
      //    r = 0;                 // Guaranteed birth the first time.
      // else
      r = random( FLARE_CHANCE_DENOMINATOR);
      // Serial.print( "r = ");
      // Serial.println( r);
      if (r == 0)
      {
         makeNewFlare( flares, nextFlareIx);
         flareCount++;
         nextFlareIx = (nextFlareIx + 1) % MAX_FLARES;
      }
   }
   
   ageFlares( flares);
   showFlares( flares);
   
   delay( DELAY_TIME);
}

// ---------------------------------------------------------------------------------------------------------------------
//  makeNewFlare
// ---------------------------------------------------------------------------------------------------------------------

// Make new flare at nextFlareIx

void makeNewFlare( struct flare *flares, int nextFlareIx)
{
   int i = nextFlareIx;
   
   flares[i].isValid = 1;
   flares[i].locn = randomAvailableLocation( flares);
   flares[i].hue = random( 360);
   flares[i].age = 0;
}

// ---------------------------------------------------------------------------------------------------------------------
//  randomAvailableLocation
// ---------------------------------------------------------------------------------------------------------------------

// Find a random point not already in use in flares.

struct point randomAvailableLocation( struct flare *flares)
{
   struct point pt;
   int i;
   bool ok;

   do
   {
      pt.x = random( GRID_SIZE);
      pt.y = random( GRID_SIZE);
      ok = true;
      for (i = 0; i < MAX_FLARES; i++)
      {
         if (flares[i].isValid && pt.x == flares[i].locn.x && pt.y == flares[i].locn.y)
         {
            ok = false;
            break;
         }
      }
   } while (! ok);

   return pt;
}
   
// ---------------------------------------------------------------------------------------------------------------------
//  ageFlares
// ---------------------------------------------------------------------------------------------------------------------

// For all valid flares, increase their age, invalidating the ones that have aged out (age > MAX_AGE).

void ageFlares( struct flare *flares)
{
   int i;
   for (i = 0; i < MAX_FLARES; i++)
   {
      if (flares[i].isValid)
      {
         flares[i].age++;
         if (flares[i].age > MAX_AGE)
         {
            flares[i].isValid = 0;
            flareCount--;
         }
      }
   }
}

// ---------------------------------------------------------------------------------------------------------------------
//  showFlares
// ---------------------------------------------------------------------------------------------------------------------

// Display the current set of flares, at lightness dictated by age.

void showFlares( struct flare *flares)
{
   int i;
   int seq;
   float dLight = (1.0 - ZERO_BRIGHTNESS) / MAX_AGE;
   float light;
   struct rgbTriple rgb;
   uint32_t c;
   
   for (i = 0; i < MAX_FLARES; i++)
   {
      if (flares[i].isValid)
      {
         light = (MAX_AGE - flares[i].age) * dLight + ZERO_BRIGHTNESS;
         light = pow( light, 2); // Linear to polynomial curve
         rgb = hsl2rgb( flares[i].hue, 1.0, light);
         seq = point2seq0( flares[i].locn);
         c = Color( rgb.r, rgb.g, rgb.b);
         strip.setPixelColor( seq, c);
      }
   }
   strip.show();
}

// ---------------------------------------------------------------------------------------------------------------------
//  point2seq
// ---------------------------------------------------------------------------------------------------------------------

// Calls point2seq() with an origin of (0,0).

int point2seq0( struct point aPt)
{
   static struct point origin = {0,0};
   static struct point gridSize = {GRID_SIZE, GRID_SIZE};

   return point2seq( aPt, origin, gridSize);
}

// Convert a point to a sequence number.  Point is relative to anOrigin.  aGridSize specifies width (x) and height (y)
// of grid.  Grid is assumed to be laid out in a zigzag pattern starting at the lower left corner, continuing
// horizontally to the lower right corner, going up a row and continuing back to the left, going up a row and continuing
// to the right, etc.

int point2seq( struct point aPt, struct point anOrigin, struct point aGridSize)
{
   int retval;
   struct point pt0 = aPt;

   // Transform to zero-based point.
   pt0.x -= anOrigin.x;
   pt0.y -= anOrigin.y;

   // y-coord * number of rows in grid.  For 5x5 grid,
   //    0 ==> 0
   //    1 ==> 5
   //    2 ==> 10
   //    etc.
   retval = pt0.y * aGridSize.y;

   // x-coord.  For 5x5 grid,
   //    (0,0) ==> 0 + 0         = 0
   //    (0,1) ==> 0 + (4 - 0)   = 4
   //    (1,0) ==> 0 + 1         = 1
   //    (1,1) ==> 5 + (4 - 1)   = 8
   //    (2,2) ==> 10 + 2        = 12
   //    (2,3) ==> 15 + (4 - 2)  = 17
   //    (3,2) ==> 10 + 3        = 13
   if (pt0.y % 2 == 0)
      // Even-numbered row
      retval += pt0.x;
   else
      // Odd-numbered row
      retval += (aGridSize.x - 1 - pt0.x);
   
   return retval;
}

// ---------------------------------------------------------------------------------------------------------------------
//  hue2rgb (internal helper function)
// ---------------------------------------------------------------------------------------------------------------------

// CSS3 spec algorithm: http://www.w3.org/TR/2011/REC-css3-color-20110607/#hsl-color

float hue2rgb( float m1, float m2, float hue )
{
   float retval;
   float h;
   
   if (hue < 0.0)
      h = hue + 1.0;
   else if (hue > 1.0)
      h = hue - 1.0;
   else
      h = hue;

   if (h * 6.0 < 1.0)
      retval = m1 + (m2 - m1) * h * 6.0;
   else if (h * 2.0 < 1.0)
      retval = m2;
   else if (h * 3.0 < 2.0)
      retval = m1 + (m2 - m1) * (2.0 / 3.0 - h) * 6.0;
   else
      retval = m1;

   return retval;
}

// ---------------------------------------------------------------------------------------------------------------------
//  hsl2rgb
// ---------------------------------------------------------------------------------------------------------------------

// CSS3 spec algorithm: http://www.w3.org/TR/2011/REC-css3-color-20110607/#hsl-color
// hue in degrees
// sat, light in range [0..1]

struct rgbTriple hsl2rgb( int aHue, float aSat, float aLight)
{
   struct rgbTriple retval;
   
   float m1, m2, r, g, b;
   
   int hue = aHue;
   while (hue < 0)
      hue += 360;
   while (hue >= 360)
      hue -= 360;

   hue = adjustedHue[ hue];
   
   // In case adjustment puts us outside our range:
   while (hue < 0)
      hue += 360;
   while (hue >= 360)
      hue -= 360;

   float h = (float) hue / 360.0;
   
   if (aLight < 0.5)
      m2 = aLight * (aSat + 1.0);
   else
      m2 = aLight + aSat - aLight * aSat;
   m1 = aLight * 2.0 - m2;
   r = hue2rgb( m1, m2, h + 1.0/3.0);
   g = hue2rgb( m1, m2, h);
   b = hue2rgb( m1, m2, h - 1.0/3.0);
   retval.r = constrain( (int) (r * 256), 0, 255);
   retval.g = constrain( (int) (g * 256), 0, 255);
   retval.b = constrain( (int) (b * 256), 0, 255);

   return retval;
}

// ---------------------------------------------------------------------------------------------------------------------
//  clearStrip
// ---------------------------------------------------------------------------------------------------------------------

void clearStrip()
{
   int i;
   for (i = 0; i < 25; i++)
      strip.setPixelColor( i, 0);
}

// ---------------------------------------------------------------------------------------------------------------------
//  Adafruit WS2801 library code
// ---------------------------------------------------------------------------------------------------------------------

// The following copyright applies to all code in this section.

// Example library for driving Adafruit WS2801 pixels!


//   Designed specifically to work with the Adafruit RGB Pixels!
//   12mm Bullet shape ----> https://www.adafruit.com/products/322
//   12mm Flat shape   ----> https://www.adafruit.com/products/738
//   36mm Square shape ----> https://www.adafruit.com/products/683

//   These pixels use SPI to transmit the color data, and have built in
//   high speed PWM drivers for 24 bit color per pixel
//   2 pins are required to interface

//   Adafruit invests time and resources providing this open source code, 
//   please support Adafruit and open-source hardware by purchasing 
//   products from Adafruit!

//   Written by Limor Fried/Ladyada for Adafruit Industries.  
//   BSD license, all text above must be included in any redistribution

// Create a 24 bit color value from R,G,B
// (Code snitched from Adafruit.  Please don't sue me, y'all, for stealing your bit-shifting code.)
uint32_t Color(byte r, byte g, byte b)
{
  uint32_t c;
  c = r;
  c <<= 8;
  c |= g;
  c <<= 8;
  c |= b;
  return c;
}

// ---------------------------------------------------------------------------------------------------------------------
//  Adafruit WS2801 library code ENDS
// ---------------------------------------------------------------------------------------------------------------------
