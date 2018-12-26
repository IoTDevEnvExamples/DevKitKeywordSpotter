
int vad_signal = 0;

void model_VadCallback(void* context, int* output)
{
  // save the current voice activity level.
  vad_signal = output[0];
}