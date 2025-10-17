#include <stdio.h>
#include <stdint.h>
#include <math.h>

typedef int32_t fixed16p16;
typedef int64_t fixed32p32;

void generate(const char *prefix, const float *in, size_t cnt, float (*fcn)(float)) {
    float inv[cnt]; // 1 / (b-a) pre-calc'd
    float out[cnt];
    fixed16p16 inF[cnt];
    fixed16p16 invF[cnt];
    fixed16p16 outF[cnt];

    for (size_t i = 0; i < cnt; i++) {
        out[i] = fcn(in[i]);
        inv[i] = i == (cnt - 1) ? 0 : 1.0 / (in[i + 1] - in[i]);
        inF[i] = (fixed16p16)(in[i] * 65536.0);
        invF[i] = (fixed16p16)(inv[i] * 65536.0);
        outF[i] = (fixed16p16)(out[i] * 65536.0);
    }

    printf("static const size_t %s_sz = %u;\n", prefix, (unsigned)cnt);

    printf("static const fixed16p16 %s_in[] = {", prefix);
    for (size_t i = 0; i < cnt; i++) {
        printf("%d, ", inF[i]);
    }
    printf("};\n");

    printf("static const fixed16p16 %s_inv[] = {", prefix);
    for (size_t i = 0; i < cnt; i++) {
        printf("%d, ", invF[i]);
    }
    printf("};\n");

    printf("static const fixed16p16 %s_out[] = {", prefix);
    for (size_t i = 0; i < cnt; i++) {
        printf("%d, ", outF[i]);
    }
    printf("};\n");
}



//static float tsf_decibelsToGain(float db) { return (db > -100.f ? TSF_POWF(10.0f, db * 0.05f) : 0); }
//static float tsf_gainToDecibels(float gain) { return (gain <= .00001f ? -100.f : (float)(20.0 * TSF_LOG10(gain))); }

float db2gain(float db) { return powf(10.0, db * 0.05); }
float gain2db(float gain) { return 20.0 * log10(gain); }

float minisqrt(float x) { return sqrtf(x); }

int main(int argc, char **argv) {
    float db[] = { -100, -90, -80, -70, -60, -50, -40, -30, -20, -19, -18, -17, -16, -15, -14, -13, -12, -11, -10, -9, -8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 30, 35, 40, };
    float gain[] = { 0.00001, 0.00002, 0.00003, 0.00004, 0.00006, 0.00009, 0.00013, 0.00019, 0.00029, 0.00044, 0.00066, 0.00098, 0.00148, 0.00221, 0.00332, 0.00498, 0.00747, 0.01121, 0.01682, 0.02522, 0.03783, 0.05675, 0.08513, 0.12769, 0.14046, 0.15450, 0.16996, 0.18695, 0.20565, 0.22621, 0.24883, 0.27371, 0.30109, 0.33119, 0.36431, 0.40075, 0.44082, 0.48490, 0.53339, 0.58673, 0.64540, 0.70995, 0.78094, 0.85903, 0.94494, 1.03943, 1.14337, 1.25771, 1.38348, 1.52183, 1.67401, 1.84141, 2.02556, 2.22811, 2.45092, 2.69602, 2.96562, 3.26218, 3.58840, 3.94724, 4.34196, 4.77616, 5.25377, 5.77915, 6.35706, 6.99277, 7.69205, 8.46125, 9.30738, 11.63422, 14.54277, 18.17847, 22.72308, 28.40386, 35.50482, 44.38103, 55.47628, 69.34535, 86.68169, 108.35211, 135.44014, 169.30017, 211.62522, 264.53152, 330.66440, };
    float sqrt[] = { 0.0, 0.02, 0.05, 0.1, 0.2, 0.3, 0.4, 0.5 };

    generate("tsf_db2gain", db, sizeof(db) / sizeof(db[0]), db2gain);
    generate("tsf_gain2db", gain, sizeof(gain) / sizeof(gain[0]), gain2db);
    generate("tsf_sqrtf", sqrt, sizeof(sqrt) / sizeof(sqrt[0]), minisqrt);
    return 0;
}
