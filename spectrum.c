// $Id$
// Spectral analysis service - far from complete!
// Copyright 2023, Phil Karn, KA9Q
#define _GNU_SOURCE 1
#include <assert.h>
#include <pthread.h>
#include <string.h>
#include <math.h>
#include <complex.h>
#include <fftw3.h>

#include "misc.h"
#include "iir.h"
#include "filter.h"
#include "radio.h"

// Spectrum analysis thread
void *demod_spectrum(void *arg){
  assert(arg != NULL);
  struct demod * const demod = arg;  
  
  {
    char name[100];
    snprintf(name,sizeof(name),"spect %u",demod->output.rtp.ssrc);
    pthread_setname(name);
  }

  if(demod->filter.out)
    delete_filter_output(&demod->filter.out);
  int const blocksize = demod->output.samprate * Blocktime / 1000.0F;
  demod->filter.out = create_filter_output(Frontend.in,NULL,blocksize,SPECTRUM);
  if(demod->filter.out == NULL){
    fprintf(stdout,"unable to create filter for ssrc %lu\n",(unsigned long)demod->output.rtp.ssrc);
    goto quit;
  }
  if(demod->spectrum.bin_data == NULL)
    demod->spectrum.bin_data = calloc(demod->spectrum.bin_count,sizeof(*demod->spectrum.bin_data));

  int integration_counter = 0;
  int next_jobnum = 0;

  while(!demod->terminate){
    int integration_limit = 1000.0f * demod->spectrum.integrate_time / Blocktime; // Truncate to integer
    if(integration_limit < 1)
      integration_limit = 1;  // enforce reasonableness

    // Although we don't use filter_output, demod->filter.min_IF and max_IF still need to be set
    // so radio.c:set_freq() will set the front end tuner properly

    // Determine bin for lowest requested frequency
    float const blockrate = 1000.0f / Blocktime; // Width in Hz of frequency bins (> FFT bin spacing)
    int const N = Frontend.L + Frontend.M - 1;
    float const spacing = (1 - (float)(Frontend.M-1)/N) * blockrate; // Hz between FFT bins (< FFT bin width)

    int binsperbin = demod->spectrum.bin_bw / spacing; // FFT bins in each noncoherent integration bin
    if(binsperbin < 1)
      binsperbin = 1; // enforce reasonableness

    if(downconvert(demod) == -1)
      break; // received terminate


    // Only one shared set of frequency domain buffers from forward FFT
    complex float const * const fdomain = Frontend.in->fdomain[next_jobnum++ % ND]; // Walk through circular set of fdomain buffers
    int startbin = 0; // write this ************** compute from demod->filter.bin_shift

    // Need to break out early for partial tuner coverage *****
    for(int i=0; i < demod->spectrum.bin_count; i++){ // For each noncoherent integration bin
      for(int j=0; j < binsperbin; j++){ // Add energy of each fft bin part of this integration bin
	demod->spectrum.bin_data[i] += cnrmf(fdomain[startbin + j*binsperbin]);
      }
    }
    if(++integration_counter >= integration_limit){
      // Dump integrators to network
      // write this ****************
      // Reset integrator
      integration_counter = 0;
      memset(demod->spectrum.bin_data,0,integration_limit * sizeof(*demod->spectrum.bin_data));
    }
  }
 quit:;
  if(demod->filter.energies)
    free(demod->filter.energies);
  demod->filter.energies = NULL;
  delete_filter_output(&demod->filter.out);
  return NULL;
}
