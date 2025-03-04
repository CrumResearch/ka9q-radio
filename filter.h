// $Id: filter.h,v 1.54 2022/12/29 05:38:12 karn Exp $
// General purpose filter package using fast convolution (overlap-save)
// and the FFTW3 FFT package
// Generates transfer functions using Kaiser window
// Optional output decimation by integer factor
// Complex input and transfer functions, complex or real output
// Copyright 2017, Phil Karn, KA9Q, karn@ka9q.net
#ifndef _FILTER_H
#define _FILTER_H 1

#include <pthread.h>
#include <complex.h>
#include <stdbool.h>
#include <fftw3.h>

extern double Fftw_plan_timelimit;
extern char const *Wisdom_file;
extern int Nthreads;

// Input can be REAL or COMPLEX
// Output can be REAL, COMPLEX, CROSS_CONJ, i.e., COMPLEX with special cross conjugation for ISB, or SPECTRUM (noncoherent power)
enum filtertype {
  NONE,
  COMPLEX,
  CROSS_CONJ,
  REAL,
  SPECTRUM,
};

// Input and output arrays can be either complex or real
// Used to be a union, but was prone to errors
struct rc {
  float * restrict r;
  complex float * restrict c;
};

#define ND 4
struct filter_in {
  enum filtertype in_type;           // REAL or COMPLEX
  int ilen;                          // Length of user portion of input buffer, aka 'L'
  int bins;                          // Total number of frequency bins. Complex: L + M - 1;  Real: (L + M - 1)/2 + 1
  int impulse_length;                // Length of filter impulse response, aka 'M'
  int wcnt;                          // Samples written to unexecuted input buffer
  struct rc input_buffer;            // Actual time-domain input buffer, length N = L + M - 1
  struct rc input;                   // Beginning of user input area, length L
  fftwf_plan fwd_plan;               // FFT (time -> frequency)

  pthread_mutex_t filter_mutex;      // Synchronization for sequence number
  pthread_cond_t filter_cond;

  complex float *fdomain[ND];
  unsigned int next_jobnum;
  unsigned int completed_jobs[ND];
};

struct filter_out {
  struct filter_in * restrict master;
  enum filtertype out_type;          // REAL, COMPLEX or CROSS_CONJ
  int olen;                          // Length of user portion of output buffer (decimated L)
  int bins;                          // Number of frequency bins; == N for complex, == N/2 + 1 for real output
  complex float * restrict fdomain;          // Filtered signal in frequency domain
  complex float * restrict response;           // Filter response in frequency domain
  pthread_mutex_t response_mutex;
  struct rc output_buffer;           // Actual time-domain output buffer, length N/decimate
  struct rc output;                  // Beginning of user output area, length L/decimate
  fftwf_plan rev_plan;               // IFFT (frequency -> time)
  unsigned int next_jobnum;
  float noise_gain;                  // Filter gain on uniform noise (ratio < 1)
  int block_drops;                   // Lost frequency domain blocks, e.g., from late scheduling of slave thread
  int rcnt;                          // Samples read from output buffer
};

int window_filter(int L,int M,complex float * restrict response,float beta);
int window_rfilter(int L,int M,complex float * restrict response,float beta);

struct filter_in *create_filter_input(int const L,int const M, enum filtertype const in_type);
struct filter_in *create_filter_input_file(int const L,int const M, enum filtertype const in_type,char * restrict file);
struct filter_out *create_filter_output(struct filter_in * restrict master,complex float * restrict response,int olen, enum filtertype out_type);
int execute_filter_input(struct filter_in * restrict);
int execute_filter_output(struct filter_out * restrict ,int);
int execute_filter_output_idle(struct filter_out * const slave);
int delete_filter_input(struct filter_in ** restrict);
int delete_filter_output(struct filter_out ** restrict);
int make_kaiser(float * restrict,int M,float);
int set_filter(struct filter_out * restrict,float,float,float);
float const noise_gain(struct filter_out const * restrict);
void *run_fft(void *);
int write_cfilter(struct filter_in *, complex float const *,int size);
int write_rfilter(struct filter_in *, float const *,int size);


// Write complex samples to input side of filter
static inline int put_cfilter(struct filter_in * restrict const f,complex float const s){ // Complex
  f->input.c[f->wcnt] = s;
  if(++f->wcnt == f->ilen){
    f->wcnt = 0;
    execute_filter_input(f);
    return 1; // may now execute filter output without blocking
  }
  return 0;
}

static inline int put_rfilter(struct filter_in * restrict const f,float const s){
  f->input.r[f->wcnt] = s;
  if(++f->wcnt == f->ilen){
    f->wcnt = 0;
    execute_filter_input(f);
    return 1; // may now execute filter output without blocking
  }
  return 0;
}

// Read real samples from output side of filter
static inline float read_rfilter(struct filter_out * restrict const f,int const rotate){
  if(f->rcnt == 0){
    execute_filter_output(f,rotate);
    f->rcnt = f->olen;
  }
  return f->output.r[f->olen - f->rcnt--];
}

// Read complex samples from output side of filter
static inline complex float read_cfilter(struct filter_out * restrict const f,int const rotate){
  if(f->rcnt == 0){
    execute_filter_output(f,rotate);
    f->rcnt = f->olen;
  }
  return f->output.c[f->olen - f->rcnt--];
}

#endif
