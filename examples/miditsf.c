#define TSF_IMPLEMENTATION
#include "../tsf.h"
#define PROGMEM
#include "furelise.c"

// Holds the global instance pointer
static tsf* g_TinySoundFont;

static FILE *out;
const int renderMax = 256;
static short *data = NULL;
static void Render(int cnt)
{
	// Note we don't do any thread concurrency control here because in this
	// example all notes are started before the audio playback begins.
	// If you do play notes while the audio thread renders output you
	// will need a mutex of some sort.
	if (!data) data = (short *)malloc(sizeof(short) * renderMax * 2);
	while (cnt) {
		int toRender = (cnt > renderMax)? renderMax : cnt;
		tsf_render_short(g_TinySoundFont, data, toRender, 0);
		fwrite(data, toRender, 2*sizeof(short), out);
		cnt -= toRender;
	}
	fflush(out);
}

int main(int argc, char *argv[])
{
	// Load the SoundFont from the memory block
	g_TinySoundFont = tsf_load_filename("kawai.sf2");
	if (!g_TinySoundFont)
	{
		fprintf(stderr, "Could not load soundfont\n");
		return 1;
	}

	out = fopen("raw.bin", "wb");

	// Set the rendering output mode to 44.1khz and -10 decibel gain
	tsf_set_output(g_TinySoundFont, TSF_STEREO_INTERLEAVED, 44100, -10);


	unsigned char inst[16];
	unsigned char key[16];
	int p=0;
	while (score[p]!=0xe0 && score[p]!=0xf0) {
		if (score[p]&0x80) {
			if ((score[p]&0xf0) == 0x90) {
				unsigned char t = score[p]&0x0f;
				unsigned char nn = score[++p];
				unsigned char vv = score[++p];
				tsf_note_on(g_TinySoundFont, inst[t], nn + 0, vv / 256.0);
				key[t] = nn;
			} else if ((score[p]&0xf0) == 0x80) {
				unsigned char t = score[p]&0x0f;
				tsf_note_off(g_TinySoundFont, inst[t], key[t]);
			} else if ((score[p]&0xf0) == 0xc0) {
				unsigned char t = score[p]&0x0f;
				unsigned char ii = score[++p];
				inst[t] = ii;
			}
			p++;
		} else {
			unsigned short delay = (score[p]<<8) | (score[p+1]);
			int samples = (((int)delay) * 44100) / 1000;
			Render(samples);
			p+= 2;
		}
	}
	Render(44100 / 2); // .5s fade out
	fclose(out);

	tsf_close(g_TinySoundFont);

	return 0;
}
