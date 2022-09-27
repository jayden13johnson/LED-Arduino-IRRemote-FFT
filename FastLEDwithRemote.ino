//parts of IR code adapted from https://www.makerguides.com/ir-receiver-remote-arduino-tutorial/
//parts of FFT code adapted from video https://www.youtube.com/watch?v=Mgh2WblO5_c and corresponding code https://github.com/s-marley/ESP32_FFT_VU
 


#include <arduinoFFT.h>
#include <FastLED.h>
#include <IRremote.h> // include the IRremote library



#define RECEIVER_PIN 2 // define the IR receiver pin
#define SAMPLES         64          // Must be a power of 2
#define SAMPLING_FREQ   9800         // Hz, must be 40000 or less due to ADC conversion time. Determines maximum frequency that can be analysed by the FFT Fmax=sampleF/2.
#define AMPLITUDE       500          // Depending on your audio source level, you may need to alter this value. Can be used as a 'sensitivity' control.
#define AUDIO_IN_PIN    A0            // Signal in on this pin
#define LED_PIN         6             // LED strip data
#define COLOR_ORDER     GRB           // If colours look wrong, play with this
#define CHIPSET         WS2812B       // LED strip type
#define NUM_BANDS       8            // To change this, you will need to change the bunch of if statements describing the mapping from bins to bands
#define NOISE           580           // Used as a crude noise filter, values below this are ignored
#define NUM_LEDS        220    // Total number of LEDs
#define Brightness      70    // Brightness level (0-255)



// FFT variables/setup 
unsigned int sampling_period_us;
double vReal[SAMPLES];
double vImag[SAMPLES];
unsigned long newTime;
int bandValues[] = {0,0,0,0,0,0,0,0};
arduinoFFT FFT = arduinoFFT(vReal, vImag, SAMPLES, SAMPLING_FREQ);

//Variables for determining RGB values using FFT light function, setting in middle of 0-255 range to begin with. 
int actual_bass_intensity = 127; 
int actual_trebel_intensity = 127;
int actual_middle_intensity = 127;

// IR remote variables/setup
unsigned long key_value = 0; // variable to store the key value
int state = 0;
int previous_state = 1; //if just plugged in and turned on, default state is 1
IRrecv receiver(RECEIVER_PIN); // create a receiver object of the IRrecv class
decode_results results; // create a results object of the decode_results class

//FastLED setup
CRGB leds[NUM_LEDS];  


// Light Functions
uint8_t hue = 0;

void fillhue() {
   for(int i = 0; i < NUM_LEDS; i++) {
   leds[i] = CHSV(hue + (i*10), 255, 255);
  }
  EVERY_N_MILLISECONDS(5){
    hue++;
  }
  FastLED.show(); 
}
void fillfunction(){
 fill_solid(leds, NUM_LEDS, CRGB::Red);
 FastLED.show();
 delay(500);

 fill_solid(leds, NUM_LEDS, CRGB::Green);
 FastLED.show();
 delay(500);

 fill_solid(leds, NUM_LEDS, CRGB::Blue);
 FastLED.show();
 delay(500);
}

void off() { 
   fill_solid(leds, NUM_LEDS, CRGB::Black);
 FastLED.show();
 delay(500);
}

void fillFFT() {
     for (int i = 0; i < SAMPLES; i++) {
        newTime = micros();
        vReal[i] = analogRead(AUDIO_IN_PIN); // A conversion takes about 100 uS on MEGA2560
        vImag[i] = 0;
        while ((micros() - newTime) < sampling_period_us) { /* chill */ } //Waiting until enough time has elapsed (~100uS) so that another sample is able to be taken
      }
      
        // Compute FFT
      FFT.DCRemoval();
      FFT.Windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
      FFT.Compute(FFT_FORWARD);
      FFT.ComplexToMagnitude();
    
      
    
      // Analyse FFT results
      for (int i = 1; i < (SAMPLES/2); i++){       // Don't use sample 0 and only first SAMPLES/2 are usable. Each array element represents a frequency bin and its value the amplitude.
        if (vReal[i] > NOISE) {                    // Add a noise filter
          if (i<=1)           bandValues[0]  += (int)vReal[i];  //Bass is bands 0 & 1  (0 - 144 Hz)
          if (i>1   && i<=2  ) bandValues[1]  += (int)vReal[i]; 
          if (i>2   && i<=5 ) bandValues[2]  += (int)vReal[i];  //Middle is bands 2 & 3 (144 - 467 Hz)
          if (i>5  && i<=8 ) bandValues[3]  += (int)vReal[i];  
          if (i>8  && i<=15 ) bandValues[4]  += (int)vReal[i];  //Trebel is bands 4 & 5 (467 - 1512 Hz)
          if (i>15  && i<=27) bandValues[5]  += (int)vReal[i];  
          if (i>27 && i<=49) bandValues[6]  += (int)vReal[i];   //High is bands 6 & 7 (1512 - 4900 Hz). 4900 Hz is our Nyquist rate with the Mega 2560 sampling rate of 9800. This band does not contribute to color of lights. 
          if (i>49          ) bandValues[7]  += (int)vReal[i]; 
        }
      }


      /* Experimentally found approx max values for each tone of sound by looking at the bands that tone is composed of and testing max observed values using variety of music. 
       There tends to be a higher amplitude when it comes to trebel and high noises, so they are very large compared to others. by doing this, we can balance and scale them all to a 0-255 scale. */
      int max_observed_amplitude_bass = 6000;
       int max_observed_amplitude_mid = 10000;
        int max_observed_amplitude_trebel = 60000;
        int max_observed_amplitude_high = 30000;

     //To determine intensity of given tone, take the max value of the 2 bands that it is composed of, then map it from 0-(its experimentally determined maximum value) to 0-255
     int bass_intensity = abs(map(max(bandValues[0], bandValues[1]), 0, max_observed_amplitude_bass, 0, 255)); //Blue
     int middle_intensity = abs(map(max(bandValues[2], bandValues[3]), 0, max_observed_amplitude_mid, 0, 255)); //Green
     int trebel_intensity = abs(map(max(bandValues[4], bandValues[5]), 0, max_observed_amplitude_trebel, 0, 255)); //Red
     int high_intensity = abs(map(max(bandValues[6], bandValues[7]), 0, max_observed_amplitude_high, 0, 255)); //Ignored


    //changing our RGB colors based on how different current bass/middle/trebel intensity is from previous. RGB values can only move up or down by maximum of 20 per cycle to reduce erratic behavior.
    if (actual_bass_intensity < bass_intensity) {
       actual_bass_intensity = actual_bass_intensity + min((bass_intensity - actual_bass_intensity), 100);  //Bass can increase by up to 100 per cycle as an exception to allow for cool visual effect. Bass tends to be quicker pulses than middle/trebel, so an exception is made for this reason.
     }
    else {
       actual_bass_intensity = actual_bass_intensity - min((actual_bass_intensity - bass_intensity), 20);
    }
    
    if (actual_middle_intensity < middle_intensity) {
       actual_middle_intensity = actual_middle_intensity + min((middle_intensity -  actual_middle_intensity), 20);
     }
     else {
       actual_middle_intensity = actual_middle_intensity - min((actual_middle_intensity - middle_intensity), 20);
    }
    
     if (actual_trebel_intensity < trebel_intensity) {
       actual_trebel_intensity = actual_trebel_intensity + min((trebel_intensity - actual_trebel_intensity), 20);
     }
      else {
       actual_trebel_intensity = actual_trebel_intensity - min((actual_trebel_intensity - trebel_intensity), 20);
    }
    
      int i;
      for (i = 0; i < NUM_LEDS; i++) {
         leds[i] = CRGB(actual_trebel_intensity, actual_middle_intensity, actual_bass_intensity);
      }
      FastLED.show(); //display new RGB values
         
      int j;
      for(j=0; j < NUM_BANDS; j++) {
        bandValues[j] = 0;   //reset band values
      }
}





void setup() {
  Serial.begin(115200);
  sampling_period_us = round(1000000 * (1.0 / SAMPLING_FREQ));
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(Brightness); 
  FastLED.setMaxPowerInVoltsAndMilliamps( 5, 4000);  
  receiver.enableIRIn(); // enable the receiver
  receiver.blink13(true); // enable blinking of the built-in LED when an IR signal is received
  }


void loop() {
switch (state) { //run function corresponding to what button was last pressed
    case 0:
        off();
        Serial.println("off");
        break;
    case 1: 
       Serial.println("fillhue");
        fillhue();
        break;
    case 2:
        Serial.println("fillfunction");
        fillfunction();
        break; 
    case 3:
      //  Serial.println("fillFFT");
        fillFFT();
        break;
        
    //remaining 5 buttons can be used for more unique patterns
    case 4:
        Serial.println("fillhue");
        fillhue();
        break;
    case 5:
        Serial.println("fillfunction");
        fillfunction();
        break;
    case 6:
        Serial.println("fillhue");
        fillhue();
        break;
    case 7:
        Serial.println("fillfunction");
        fillfunction();
        break;
    case 8:
        Serial.println("fillhue");
        fillhue();
        break; 
       }

  //IR remote stuff
  while (!receiver.isIdle());  // if not idle, wait till complete
  if (receiver.decode(&results)) { // decode the received signal and store it in results
    
    Serial.println(results.value, HEX); // print the values in the Serial Monitor
    int x = state;
    switch (results.value) { // compare the value to the following cases
      case 0xFF629D:
      case 0x511DBB: // Power Button
        if (state == 0) {  // If Off (turning lights on)
          if (previous_state == 0) { //Make sure previous_state and state both don't equal 0 at same time (would cause power button to not work as intended)
            state = 1;
          }
          else { //otherwise, state becomes what lights were before turning off
            state = previous_state;
          }
        }
        else { // If already on
          state = 0; // 
        }
        Serial.println(state);
        break;
    }
    if (state != 0 && x !=0) {   //if state is not OFF and lights didnt just get turned ON 
      switch (results.value) {
         case 0xFF22DD: //A
          state = 1;
          Serial.println(state);
          break;
        case 0xFF02FD: //B
          state = 2;
          Serial.println(state);
          break;
        case 0xFFC23D: //C
          state = 3;
          Serial.println(state);
          break;
        case 0xFF9867:  //up arrow
          state = 4;
          Serial.println(state);
          break;
        case 0xFF30CF:  //left arrow
          state = 5;
          Serial.println(state);
          break;
        case 0xFF18E7:  //center button
          state = 6;
          Serial.println(state);
          break;
        case 0xFF7A85:  // right arrow
          state = 7;
          Serial.println(state);
          break;
        case 0xFF38C7:  // down arrow
          state = 8;
          Serial.println(state);
          break;
         }
      }
     if (x != state) {
      previous_state = x;
     } 
      key_value = results.value; // store the value as key_value 
    receiver.resume(); // reset the receiver for the next code
    }
}
