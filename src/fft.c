#include <assert.h>
#include <complex.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>

float pi;

// Clear floats
#define EPS 1e-5f
void fft(float in[], int stride, float complex out[], size_t size) {

  assert(size > 0);

  if (size == 1) {
    out[0] = in[0 * stride];
    return;
  }
  fft(in, stride * 2, out, size / 2);
  fft(in + stride, stride * 2, out + size / 2, size / 2);

  for (size_t k = 0; k < size / 2; k++) {
    float t = (float)k / size;
    float complex v = cexp(-I * 2 * pi * t) * out[k + size / 2];
    float complex e = out[k];

    out[k] = e + v;
    out[k + size / 2] = e - v;
  }
}
int main() {
  printf("This is a fft test\n");
  float in[8];
  for (int n = 0; n < 8; n++) {
    in[n] = sinf(2.0f * M_PI * 8.0f * n / 8.0f);
  }

  float complex out[8];

  fft(in, 1, out, 8);

  for (size_t i = 0; i < 8; i++) {

    if ((i % 8) == 0) {
      printf("\n");
    }
    float re;
    float im;
    if (fabsf(crealf(out[i])) < EPS)
      re = 0.0f;
    if (fabsf(cimagf(out[i])) < EPS)
      im = 0.0f;
    printf("z = %f + %fi\n", re, im);
  }
  return 0;
}
