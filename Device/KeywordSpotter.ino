#include <Arduino.h>
#include <stdio.h>
#include <math.h>
#include "OledDisplay.h"
#include "AudioClassV2.h"
#include "stm32412g_discovery_audio.h"
#include "RGB_LED.h"
#include <stdint.h>
#include <SystemTickCounter.h>

#define MFCC_WRAPPER_DEFINED
#include "featurizer.h"
#define MODEL_WRAPPER_DEFINED
#include "classifier.h"

// If you get compile errors you may need to delete the temporary build folder.
#include "buttons.h"
#include "simpletimer.h"

#include "SystemTickCounter.h"
#include "instructioncounter.h"

// Global variables
AudioClass& Audio = AudioClass::getInstance();

enum AppState 
{
  APPSTATE_Init,
  APPSTATE_Error,
  APPSTATE_Recording
};

static AppState appstate;

// These numbers need to match the compiled ELL models.
const int SAMPLE_BIT_DEPTH = 16;
#include "model_properties.h"
const float THRESHOLD = 0.9;

static int scaled_input_buffer_pos = 0;
static float scaled_input_buffer[FEATURIZER_INPUT_SIZE]; // raw audio converted to float

const int MAX_FEATURE_BUFFERS = 10; // set to buffer up to 1 second of audio in circular buffer
static float featurizer_input_buffers[MAX_FEATURE_BUFFERS][FEATURIZER_INPUT_SIZE]; // input to featurizer
static int featurizer_input_buffer_read = -1; // next read pos
static int featurizer_input_buffer_write = 0; // next write pos
static int dropped_frames = 0;
static float featurizer_output_buffer[FEATURIZER_OUTPUT_SIZE]; // 40 channels
static float classifier_output_buffer[CLASSIFIER_OUTPUT_SIZE]; // 31 classes

static int raw_audio_count = 0;
static char raw_audio_buffer[AUDIO_CHUNK_SIZE];
static int prediction_count = 0;
static uint last_prediction = 0;
static int last_confidence = 0; // as a percentage between 0 and 100.

static uint8_t maxGain = 0;
static uint8_t minGain = 0;

static SimpleTimer hint_timer;
static uint hint_index = 1;
static uint hint_delay = 5; // seconds

extern "C" {
    extern int vad_signal;
}

ButtonManager buttons;
InstructionCounter cycleCounter;

int next(int pos){
  pos++;
  if (pos == MAX_FEATURE_BUFFERS){
    pos = 0;
  }
  return pos;
}

#include "categories.h"

uint max_category = 0;
int last_vad = 0;
float min_level = 100;
float max_level = 0;
RGB_LED rgbLed;

// This helper function uses the RGB led to give an indication of audio levels (brightness)
// and voice activity (red)
void show_signals(int vad, float level)
{
  if (level < min_level){
    min_level = level;
  }
  if (level > max_level){
    max_level = level;
  }
  int red = 0;
  int green = 0;
  int blue = 0;
  if (vad != last_vad)
  {
    Serial.printf("VAD signal changed from %d to %d\r\n", last_vad, vad);
    last_vad = vad;  
  }

  float range = max_level - min_level;
  if (range == 0) range = 1;
  float brightness = 255 * (level - min_level) / range;
  if (vad) {
    red = brightness;
  } else {
    green = brightness + 30;
    if (green > 255)
        green = 255;
  }
  rgbLed.setColor(red, green, blue);

}

void reset_signals()
{
  min_level = 100;
  max_level = 0;
  last_vad = 0;
}

static uint reset_delay = 1; // s\econds
static bool reset_waiting = false;
static SimpleTimer reset_timer;

// During no voice activity it is helpful to reset the model every so often to stop
// the GRU from accumulating too much useless noise.
void delayed_reset(int vad)
{
  if (vad == 0)
  {
    // no voice, so start delay timer.
    if (!reset_waiting) {
      reset_waiting = true;
      reset_timer.start();
    } else {
      double s = reset_timer.seconds();
      if (s > reset_delay){
        model_Reset();
        Serial.print("#");        
        reset_timer.start();
      }
    }
  }
  else 
  {
    // no reset during voice activity
    reset_waiting =false;
    reset_timer.stop();
  }
}

// If no words are being recognized, display a hint to the user on which words they can speak.
void display_hint(bool reset)
{
  if (reset) {
    hint_timer.start();
  }
  else 
  {
    hint_timer.stop();
    double s = hint_timer.seconds();
    if (s > hint_delay) {
      Screen.clean();
      Screen.print(0, "Try saying...");
      Screen.print(1, categories[hint_index++]);
      if (hint_index == max_category) {
        hint_index = 1;
      } 
      hint_timer.start();
    }
  }
}

// Show on the screen what the current gain levels are.  User can change the min and max
// gain using the "A" and "B" buttons.
void display_gain()
{
  char buffer[20];
  Screen.clean();
  Screen.print(0, "Gain :  ");  
  if (maxGain == 0)
  {
    Screen.print(1, "off");
  }
  else
  {
    sprintf(buffer, "min=%d", (int)minGain);
    Screen.print(1, buffer);
    sprintf(buffer, "max=%d", (int)maxGain);
    Screen.print(2, buffer);
  }
}

// Set the microphone gain on the NAU88C10 audio codec chip.
void set_gain()
{
  if (maxGain < minGain) {
    maxGain = minGain;
  }

  if (maxGain == 0) {
    Audio.disableLevelControl();
  }
  else {
    Serial.printf("Enabling ALC max gain %d and min gain %d\r\n", maxGain, minGain);
    Audio.enableLevelControl(maxGain, minGain);
    delay(100);
    int value = Audio.readRegister(0x20);
    Serial.printf("Register %x = %x\r\n", 0x20, value);  
  }

  reset_signals();
  model_Reset();
}

// Increment the microphone min or max gain on the NAU88C10 audio codec chip.
void increase_gain(bool inc_min, bool inc_max)
{
  if (inc_min) {
    minGain++;
  }
  if (inc_max) {
    maxGain++;
  }
  if (maxGain > 7) {
    maxGain = 0; // wrap around
    minGain = 0;
  }
  if (minGain > 7) {
    minGain = 0;
  }
  set_gain();
  display_gain();
}

// show  an error message on the LCD screen
void show_error(const char* msg)
{
  Screen.clean();
  Screen.print(0, msg);
  appstate = APPSTATE_Error;
}

// Check the button state and for an "A" button press increment the minimum gain
// and for the "B" button increment the maximum gain.
void check_buttons()
{
  auto state = buttons.read();
  if ((state & ButtonManager::ButtonStates::ButtonAPressed) != 0)
  {
    increase_gain(true, false);
  }
  if ((state & ButtonManager::ButtonStates::ButtonBPressed) != 0)
  {
    increase_gain(false, true);
  }
}

// Process a input buffer through the featurizer and classifier to see if we can
// spot one of the keywords in categories.h.  A bit of smoothing is done on the 
// predictions so the screen doesn't update too often.  
bool get_prediction(float* featurizer_input_buffer)
{
  // looks like <chrono> doesn't work, rats...
  SimpleTimer timer;
  timer.start();

  // mfcc transform
  mfcc_Filter(nullptr, featurizer_input_buffer, featurizer_output_buffer);

  // classifier
  model_Predict(nullptr, featurizer_output_buffer, classifier_output_buffer);

  // calculate a sort of energy level from the mfcc output 
  float level = 0;
  for (int i = 0; i < FEATURIZER_OUTPUT_SIZE; i++)
  {
    float x = featurizer_output_buffer[i];
    level += (x*x);
  }
  
  int vad = vad_signal;
  show_signals(vad, level);

  float max = -1;
  uint argmax = 0;
  
  // argmax over predictions.
  for (uint j = 0; j < CLASSIFIER_OUTPUT_SIZE; j++)
  {
      float v = classifier_output_buffer[j];
      if (v > max) {
        max = v;
        argmax = j;
      }
  }

  timer.stop();
  float elapsed = timer.milliseconds();
  int percent = (int)(max * 100);  
  bool got_prediction = false;
  if (argmax != 0 && max > THRESHOLD) 
  { 
    if (last_prediction != argmax)
    {
      last_confidence = 0;
    }
    if (last_prediction != argmax || percent > last_confidence) {
      
      if (percent > last_confidence)
      {        
        // Serial.printf("Prediction %d is %s (%.2f) on level %.2f, peak voice %.2f, silence %.2f, vad=%d, in %d ms\r\n", prediction_count++, categories[argmax], (float)percent/100, level, peak_voice, silence_level, vad, (int)elapsed);
        if (last_prediction != argmax && vad && argmax < max_category) {
          //Serial.printf("Prediction %d is %s (%.2f) on level %.2f, peak voice %.2f, silence %.2f, vad=%d, in %d ms\r\n", prediction_count++, categories[argmax], (float)percent/100, level, peak_voice, silence_level, vad, (int)elapsed);
          Serial.printf("Prediction %d is %s (%.2f) on level %.2f, vad=%d, in %d ms\r\n", prediction_count++, categories[argmax], (float)percent/100, level, vad, (int)elapsed);
          Screen.clean();
          Screen.print(0, categories[argmax]);
          char line[120];
          sprintf(line, "%d %%", percent);
          Screen.print(1, line);
          if (argmax == hint_index)
          {
            hint_index++;
            if (hint_index == max_category) 
            {
              hint_index = 1;
            } 
          }
          sprintf(line, "%d ms", (int)elapsed);
          Screen.print(2, line);
          got_prediction = true;
        }
        
        last_prediction = argmax;
        last_confidence = percent;
      }
    }
    if (last_confidence > 0)
    {
      // to smooth the predictions a bit, it doesn't make sense to get two different predictions 30ms apart, so this
      // little count down on the previous confidence level provides this smoothing effect.
      last_confidence -= 1;
    }
  }
  return got_prediction;
}

// This function is called when a new recorded audio input buffer is ready to be processed.
// This is an interrupt callback so we want to make this as quick as possible, we don't return
// the classifier here, instead we store the data in a circular set of 10 input buffers.
void audio_callback()
{
  // this is called when Audio class has a buffer full of audio, the buffer is size AUDIO_CHUNK_SIZE (512)  
  Audio.readFromRecordBuffer(raw_audio_buffer, AUDIO_CHUNK_SIZE);
  raw_audio_count++;
  
  char* curReader = &raw_audio_buffer[0];
  char* endReader = &raw_audio_buffer[AUDIO_CHUNK_SIZE];
  while(curReader < endReader)
  {
    if (SAMPLE_BIT_DEPTH == 16)
    {
      // We are getting 512 samples, but with dual channel 16 bit audio this means we are 
      // getting 512/4=128 readings after converting to mono channel floating point values.
      // Our featurizer expects 256 readings, so it will take 2 callbacks to fill the featurizer
      // input buffer.
      int bytesPerSample = 2;
      
      // convert to mono
      int16_t sample = *((int16_t *)curReader);
      curReader += bytesPerSample * 2; // skip right channel (not the best but it works for now)

      scaled_input_buffer[scaled_input_buffer_pos] = (float)sample / 32768.0f;
      scaled_input_buffer_pos++;
      
      if (scaled_input_buffer_pos == FEATURIZER_INPUT_SIZE)
      {
        scaled_input_buffer_pos = 0;
        if (next(featurizer_input_buffer_write) == featurizer_input_buffer_read)
        {
          dropped_frames++; // dropping frame on the floor since classifier is still processing this buffer
        }
        else 
        {
          memcpy(featurizer_input_buffers[featurizer_input_buffer_write], scaled_input_buffer, FEATURIZER_INPUT_SIZE * sizeof(float));                  
          featurizer_input_buffer_write = next(featurizer_input_buffer_write);
        }
      }
    }
  }
}

// Tell the audio class to start recording audio with the required sample rate and bit depth that
// matches what our model was trained on (usually 16kHz, 16 bit)
void start_recording()
{
  appstate = APPSTATE_Recording;
 
  // Re-config the audio data format
  Audio.format(SAMPLE_RATE, SAMPLE_BIT_DEPTH);
  delay(100);
  
  Serial.println("listening...");

  // Start to record audio data
  Audio.startRecord(audio_callback);
}

// This is our normal Arduino setup function, here we setup various things and we check that
// the featurizer and classifier we linked with actually match the global variables defined
// in the header model_properties.h.  We also time how long the featurizer and classifier take
// in actual instruction counts.
void setup(void)
{
  SimpleTimer::init();
  appstate = APPSTATE_Init;
  
  max_category = sizeof(categories) / sizeof(char*);
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  Serial.println("ELL Audio Demo!");
  Serial.printf("Recognizing %d keywords\n", max_category);
  
  buttons.init();

  int filter_size = mfcc_GetInputSize(0);
  if (filter_size != FEATURIZER_INPUT_SIZE)
  {
    Serial.printf("Featurizer input size %d is not equal to %d\n", filter_size, FEATURIZER_INPUT_SIZE);
    show_error("Featurizer Error");
  }
  else 
  {
    int model_size = model_GetInputSize(0);
    if (model_size != FEATURIZER_OUTPUT_SIZE)
    {
      Serial.printf("Classifier input size %d is not equal to %d\n", model_size, FEATURIZER_OUTPUT_SIZE);
      show_error("Classifier Error");
    }
    else {

      int model_output_size = model_GetOutputSize(0);
      if (model_output_size != CLASSIFIER_OUTPUT_SIZE)
      {
        Serial.printf("Classifier output size %d is not equal to %d\n", model_output_size, CLASSIFIER_OUTPUT_SIZE);
        show_error("Classifier Error");
      }
    }
  }

  if (appstate != APPSTATE_Error)
  {
    // do a warm up.  
    SimpleTimer timer;

    ::memset(featurizer_input_buffers[0], 0, FEATURIZER_INPUT_SIZE);

    timer.start();
    for (int i = 0; i < 10; i++)
    {
      // give it a whirl !!
      mfcc_Filter(nullptr, featurizer_input_buffers[0], featurizer_output_buffer);
    }
    timer.stop();
    Serial.printf("Setup featurizer took %f ms\r\n", timer.milliseconds() / 10);

    timer.start();    
    cycleCounter.Enable();
    cycleCounter.Start();
    // featurizer + classifier
    mfcc_Filter(nullptr, featurizer_input_buffers[0], featurizer_output_buffer);
    cycleCounter.Stop();
    uint32_t fc = cycleCounter.GetCount();
    cycleCounter.Start();
    model_Predict(nullptr, featurizer_output_buffer, classifier_output_buffer);
    cycleCounter.Stop();
    uint32_t cc = cycleCounter.GetCount();
    cycleCounter.Disable();
    timer.stop();

    Serial.printf("Setup featurizer+ took %f ms\r\n", timer.milliseconds());
    Serial.printf("Featurizer instruction cycles = %d\r\n", fc);
    Serial.printf("Classifier instruction cycles = %d\r\n", cc);

    // check audio gain and print the result.
    uint32_t id = Audio.readRegister(nau88c10_CHIPID_ADDR);
    if (id == NAU88C10_ID) {
      Serial.printf("Found audio device: NAU88C10\r\n");
    } else {
      Serial.printf("Found audio device: 0x%x\r\n", id);
    }

    start_recording();
    
    delay(100);
    
    // a default gain level of 6 seems to work pretty well.
    minGain = 6;
    maxGain = 6;
    set_gain();

    Screen.clean();
    
    Screen.print(0, "Listening...");
    Screen.print(1, "A = min gain");
    Screen.print(2, "B = max gain");
  }
}

// This is our normal Arduino loop function where we wait for available input buffers
// and process those through the keyword spotter, show some hints and check the buttons.
void loop(void)
{
  if (appstate != APPSTATE_Error)
  {
    while (1)
    {
      check_buttons();
    
      if (dropped_frames > 0) {
        Serial.printf("%d dropped frames\n", dropped_frames);        
        dropped_frames = 0;
      }
      // process all the buffered input frames
      while(next(featurizer_input_buffer_read) != featurizer_input_buffer_write)
      {
        featurizer_input_buffer_read = next(featurizer_input_buffer_read);
        bool found = get_prediction(featurizer_input_buffers[featurizer_input_buffer_read]);
        display_hint(found);
        check_buttons();
      }
      delay(10);
    }
  }
  delay(100);
}


