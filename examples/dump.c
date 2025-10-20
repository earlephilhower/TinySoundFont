#define TSF_SAMPLES_SHORT
#define TSF_IMPLEMENTATION
#include "../tsf.h"


FILE *dump;

const char *dump_envelope(struct tsf_envelope e) {
    static char buff[512];
    sprintf(buff, "{ .delay=%f, .attack=%f, .hold=%f, .decay=%f, .sustain=%f, .release=%f, .keynumToHold=%f, .keynumToDecay=%f }",
        e.delay, e.attack, e.hold, e.decay, e.sustain, e.release, e.keynumToHold, e.keynumToDecay);
    return buff;
}

void dump_region(const struct tsf_region *r) {
    fprintf(dump, "{\n");
    fprintf(dump, " .loop_mode=%d,\n", r->loop_mode);
    fprintf(dump, " .sample_rate=%d,\n", r->sample_rate);
    fprintf(dump, " .lokey=%u, .hikey=%u, .lovel=%u, .hivel=%u,\n", r->lokey, r->hikey, r->lovel, r->hivel);
    fprintf(dump, " .group=%u, .offset=%u, .end=%u, .loop_start=%u, .loop_end=%u,\n", r->group, r->offset, r->end, r->loop_start, r->loop_end);
    fprintf(dump, " .transpose=%d, .tune=%d, .pitch_keycenter=%d, .pitch_keytrack=%d,\n", r->transpose, r->tune, r->pitch_keycenter, r->pitch_keytrack);
    fprintf(dump, " .attenuation=%f, .attenuationF16P16=%d, .pan=%f, .panF16P16 = %d\n", r->attenuation, (int)(r->attenuation * 65536.0), r->pan, (int)(r->pan * 65536.0));
    fprintf(dump, " .ampenv=%s,\n", dump_envelope(r->ampenv));
    fprintf(dump, " .modenv=%s,\n", dump_envelope(r->modenv));
    fprintf(dump, " .initialFilterQ=%d, .initialFilterFc=%d,\n", r->initialFilterQ,r-> initialFilterFc);
    fprintf(dump, " .modEnvToPitch=%d, .modEnvToFilterFc=%d, .modLfoToFilterFc=%d, .modLfoToVolume=%d,\n", r->modEnvToPitch, r->modEnvToFilterFc, r->modLfoToFilterFc, r->modLfoToVolume);
    fprintf(dump, " .delayModLFO=%f,\n", r->delayModLFO);
    fprintf(dump, " .freqModLFO=%d, .modLfoToPitch=%d,\n", r->freqModLFO, r->modLfoToPitch);
    fprintf(dump, " .delayVibLFO=%f,\n", r->delayVibLFO);
    fprintf(dump, " .freqVibLFO=%d, .vibLfoToPitch=%d\n", r->freqVibLFO, r->vibLfoToPitch);
    fprintf(dump, "}\n");
}

const char *dump_char20(const char *p) {
    static char buff[512];
    sprintf(buff, "{%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u}",p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15], p[16], p[17], p[18], p[19]);
    return buff;
}

void dump_presets(const struct tsf_preset *p, int cnt) {
    for (int idx = 0; idx < cnt; idx++) {
        fprintf(dump, "static const struct tsf_region preset_%d_regions[] = {\n", idx);
        for (int i=0; i<p[idx].regionNum; i++) {
            dump_region(&p[idx].regions[i]);
            fprintf(dump, ",\n");
        }
        fprintf(dump, "};\n");
    }

    fprintf(dump, "static const struct tsf_preset presets[] = {\n");
    for (int idx=0; idx < cnt; idx++) {
        fprintf(dump, "{\n");
        fprintf(dump, " .presetName=%s,\n", dump_char20(p[idx].presetName));
        fprintf(dump, " .preset=%u, .bank=%u,\n", p[idx].preset, p[idx].bank);
        fprintf(dump, " .regions=preset_%d_regions,\n", idx);
        fprintf(dump, " .regionNum=%d\n", p[idx].regionNum);
        fprintf(dump, "},\n");
   }
   fprintf(dump, "};\n");
}

#ifdef TSF_SAMPLES_SHORT
void dump_shortsamples(const short *s, int cnt) {
    fprintf(dump, "static const short shortSamples[%d] = {\n", cnt);
    for (int i=0; i <cnt; i++) {
        if ((i%16) == 15) {
            fprintf(dump, "\n");
        }
        fprintf(dump, "%d, ", s[i]);
    }
    fprintf(dump, "\n};\n");
}
#else
void dump_shortsamples(const float *f, int cnt) {
    fprintf(dump, "static const float fontSamples[%d] = {\n", cnt);
    for (int i=0; i <cnt; i++) {
        if ((i%16) == 15) {
            fprintf(dump, "\n");
        }
        fprintf(dump, "%f, ", f[i]);
    }
    fprintf(dump, "\n};\n");
}
#endif

void dump_tsf(tsf* t, const char *outf) {
    dump = fopen(outf, "w");
    if (!dump) {
        fprintf(stderr, "Error, unable to open '%s' for writing\n", outf);
        return;
    }
   
    fprintf(dump, "// Soundfont Header from '%s'\n", outf);
    fprintf(dump, "#define TSF_HEADER\n");
    fprintf(dump, "#include <libtinysoundfont/tsf.h>\n");

    dump_presets(t->presets, t->presetNum);
#ifndef TSF_SAMPLES_SHORT
    dump_fontsamples(t->fontSamples, t->samplesNum);
#else
    dump_shortsamples(t->shortSamples, t->samplesNum);
#endif

    fprintf(dump, "struct tsf _tsf = {\n");
    fprintf(dump, " .presets = presets,\n");
#ifndef TSF_SAMPLES_SHORT
    fprintf(dump, " .fontSamples = fontSamples,\n");
#else
    fprintf(dump, " .shortSamples = shortSamples,\n");
#endif
    fprintf(dump, " .samplesNum = %d,\n", t->samplesNum);
    fprintf(dump, " .voices = NULL,\n");
    fprintf(dump, " .channels = NULL,\n");
    fprintf(dump, " .presetNum = %d,\n", t->presetNum);
    fprintf(dump, " .voiceNum = 0,\n");
    fprintf(dump, " .maxVoiceNum = 0,\n");
    fprintf(dump, " .voicePlayIndex = 0,\n");
    fprintf(dump, " .outputmode = TSF_STEREO_INTERLEAVED,\n");
    fprintf(dump, " .outSampleRate = 44100,\n");
    fprintf(dump, " .globalGainDB = 0,\n");
    fprintf(dump, " .refCount = NULL,\n");
    fprintf(dump, "};\n");

    fclose(dump);
}

int main(int argc, char *argv[])
{
    // Holds the global instance pointer
    tsf *tsf;
    tsf = tsf_load_filename( (argc >= 2 ? argv[1] : "florestan-subset.sf2"));
    if (!tsf) {
        fprintf(stderr, "Could not load SoundFont\n");
        return 1;
    }

    const char *out = argc >= 3 ? argv[2] : "soundfont.h";

    dump_tsf(tsf, out);

    return 0;
}
