#include <stdio.h>
#include <stdint.h>
#include <math.h>

typedef int32_t fixed16p16;
typedef int64_t fixed32p32;

static fixed16p16 tsf_decibelsToGainF16P16(fixed16p16 db) {

const fixed16p16 inx[] = { -6553600, -5898240, -5242880, -4587520, -3932160, -3276800, -2621440, -1966080, -1310720, -1245184, -1179648, -1114112, -1048576, -983040, -917504, -851968, -786432, -720896, -655360, -589824, -524288, -458752, -393216, -327680, -262144, -196608, -131072, -65536, 0, 65536, 131072, 196608, 262144, 327680, 393216, 458752, 524288, 589824, 655360, 720896, 786432, 851968, 917504, 983040, 1048576, 1114112, 1179648, 1245184, 1310720, 1376256, 1441792, 1507328, 1572864, 1638400, 1966080, 2293760, 2621440, };
const fixed16p16 outfcn[] = {0, 2, 6, 20, 65, 207, 655, 2072, 6553, 7353, 8250, 9257, 10386, 11654, 13076, 14671, 16461, 18470, 20724, 23253, 26090, 29273, 32845, 36853, 41350, 46395, 52057, 58409, 65536, 73532, 82504, 92572, 103867, 116541, 130761, 146716, 164618, 184705, 207243, 232530, 260903, 292738, 328458, 368536, 413504, 463959, 520571, 584090, 655360, 735326, 825049, 925720, 1038675, 1165413, 2072430, 3685360, 6553600, };
const fixed16p16 delbainv[] = {6553, 6553, 6553, 6553, 6553, 6553, 6553, 6553, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 13107, 13107, 13107, 0, };

    if (db < inx[0]) {
        return outfcn[0];
    }
    if (db > inx[sizeof(inx)/sizeof(inx[0]) - 1]) {
        return outfcn[sizeof(inx)/sizeof(inx[0]) - 1];
    }
    int i; 
    for (i = 0; i < inx[sizeof(inx)/sizeof(inx[0]) - 2]; i++) {
        if (inx[i + 1] > db) {
            break;
        }
    }
    fixed32p32 x_a = db - inx[i];
    fixed16p16 fb_fa = outfcn[i + 1] - outfcn[i];
    fixed32p32 out = x_a * fb_fa;
    out >>= 16;
    out *= delbainv[i];
    out >>= 16;
    out += outfcn[i];

  return out;  
}

fixed16p16 interpolateFunctionF16P16(fixed16p16 db, const fixed16p16 *inx,  const fixed16p16 *outfcn, const fixed16p16 *delbainv, const int elements) {
    if (db < inx[0]) {
        return outfcn[0];
    }
    if (db >= inx[elements - 1]) {
        return outfcn[elements - 1];
    }
    int i;
    for (i = 0; i < inx[elements - 2]; i++) {
        if (inx[i + 1] > db) {
            break;
        }
    }
    fixed32p32 x_a = db - inx[i];
    fixed16p16 fb_fa = outfcn[i + 1] - outfcn[i];
    fixed32p32 out = x_a * fb_fa;
    out >>= 16;
    out *= delbainv[i];
    out >>= 16;
    out += outfcn[i];

  return out;
}


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

    printf("static const size_t %s_sz = %u\n", prefix, cnt);

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


int main(int argc, char **argv) {
    float db[] = { -100, -90, -80, -70, -60, -50, -40, -30, -20, -19, -18, -17, -16, -15, -14, -13, -12, -11, -10, -9, -8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 30, 35, 40, };
    float gain[] = { 0.00001, 0.00002, 0.00003, 0.00004, 0.00006, 0.00009, 0.00013, 0.00019, 0.00029, 0.00044, 0.00066, 0.00098, 0.00148, 0.00221, 0.00332, 0.00498, 0.00747, 0.01121, 0.01682, 0.02522, 0.03783, 0.05675, 0.08513, 0.12769, 0.14046, 0.15450, 0.16996, 0.18695, 0.20565, 0.22621, 0.24883, 0.27371, 0.30109, 0.33119, 0.36431, 0.40075, 0.44082, 0.48490, 0.53339, 0.58673, 0.64540, 0.70995, 0.78094, 0.85903, 0.94494, 1.03943, 1.14337, 1.25771, 1.38348, 1.52183, 1.67401, 1.84141, 2.02556, 2.22811, 2.45092, 2.69602, 2.96562, 3.26218, 3.58840, 3.94724, 4.34196, 4.77616, 5.25377, 5.77915, 6.35706, 6.99277, 7.69205, 8.46125, 9.30738, 11.63422, 14.54277, 18.17847, 22.72308, 28.40386, 35.50482, 44.38103, 55.47628, 69.34535, 86.68169, 108.35211, 135.44014, 169.30017, 211.62522, 264.53152, 330.66440, };

    generate("tsf_db2gain", db, sizeof(db) / sizeof(db[0]), db2gain);
    generate("tsf_gain2db", gain, sizeof(gain) / sizeof(gain[0]), gain2db);
    
#if 0

    float deltadbinv[256];
    float out[256];
    int elems = sizeof(db)/sizeof(db[0]);
    for (int i=0; i < sizeof(db)/sizeof(db[0]); i++) {
        out[i] = powf(10.0f, db[i] * 0.05f);
        deltadbinv[i] = i ==  sizeof(db)/sizeof(db[0]) - 1? 0 : 1 / (db[i+1] - db[i]);
    }

    fixed16p16 dbF[elems];
    fixed16p16 outF[elems];
    fixed16p16 invF[elems];

    printf("const fixed16p16 inx[] = { ");
    for (int i=0; i < sizeof(db)/sizeof(db[0]); i++) {
        printf("%d, ", dbF[i] = (uint32_t)( 65536 * db[i]));
    }
    printf("};\n");

    printf("const fixed16p16 out[] = {");
    for (int i=0; i < sizeof(db)/sizeof(db[0]); i++) {
        printf("%d, ", outF[i] = (uint32_t)(65536 * out[i]));
    }
    printf("};\n");

    printf("const fixed16p16 delbainv[] = {");
    for (int i=0; i < sizeof(db)/sizeof(db[0]); i++) {
        printf("%d, ", invF[i] = (uint32_t)(65536 * deltadbinv[i]));
    }
    printf("};\n");

#if 1
    for (float x=-110.0; x < 40.09; x+= 0.1) {
        fixed16p16 z = x * 65536;
//        printf("%0.1f, %d\n", x, tsf_decibelsToGainF16P16(z));
printf("%0.1f, %d\n", x, interpolateFunctionF16P16(z, dbF, outF, invF, elems));
  }
#else
  float x= 21.1;
  fixed16p16 z = 0.5 * 65536;
  printf("%0.1f, %d\n", x, tsf_decibelsToGainF16P16(z));

#endif

#endif

    return 0;
}
