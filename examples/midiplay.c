#define TSF_IMPLEMENTATION
#include "../tsf.h"

#include "minisdl_audio.h"

#define FREQ 44100

static tsf *g_tsf;
struct tsf_stream buffer;
struct tsf_stream stdio;

/*********************************************************************************************
*
*  MIDITONES: Convert a MIDI file into a simple bytestream of notes
*
*----------------------------------------------------------------------------------------
* The MIT License (MIT)
* Copyright (c) 2011,2013,2015,2016, Len Shustek
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF, OR
* IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*********************************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <time.h>
#include <inttypes.h>


/***********  MIDI file header formats  *****************/

struct midi_header {
   int8_t MThd[4];
   uint32_t header_size;
   uint16_t format_type;
   uint16_t number_of_tracks;
   uint16_t time_division;
};

struct track_header {
   int8_t MTrk[4];
   uint32_t track_size;
};


/***********  Global variables  ******************/

#define MAX_TONEGENS 32         /* max tone generators: tones we can play simultaneously */
#define MAX_TRACKS 24           /* max number of MIDI tracks we will process */

int hdrptr;
unsigned long buflen;
int num_tracks;
int tracks_done = 0;
int num_tonegens = MAX_TONEGENS;
int num_tonegens_used = 0;
unsigned int ticks_per_beat = 240;
unsigned long timenow = 0;
unsigned long tempo;            /* current tempo in usec/qnote */

struct tonegen_status {         /* current status of a tone generator */
   bool playing;                /* is it playing? */
   bool stopnote_pending;       /* do we need to stop this generator before the next wait? */
   char track;                   /* if so, which track is the note from? */
   char note;                    /* what note is playing? */
   char instrument;              /* what instrument? */
} tonegen[MAX_TONEGENS] = {
   {
   0}
};

struct track_status {           /* current processing point of a MIDI track */
   int trkptr;                  /* ptr to the next note change */
   int trkend;                  /* ptr past the end of the track */
   unsigned long time;          /* what time we're at in the score */
   unsigned long tempo;         /* the tempo last set, in usec per qnote */
   unsigned int preferred_tonegen;      /* for strategy2, try to use this generator */
   unsigned char cmd;           /* CMD_xxxx next to do */
   unsigned char note;          /* for which note */
   unsigned char chan;          /* from which channel it was */
   unsigned char velocity;      /* the current volume */
   unsigned char last_event;    /* the last event, for MIDI's "running status" */
   bool tonegens[MAX_TONEGENS]; /* which tone generators our notes are playing on */
} track[MAX_TRACKS] = {
   {
   0}
};

int midi_chan_instrument[16] = {
   0
};                              /* which instrument is currently being played on each channel */

/* output bytestream commands, which are also stored in track_status.cmd */

#define CMD_PLAYNOTE    0x90    /* play a note: low nibble is generator #, note is next byte */
#define CMD_STOPNOTE    0x80    /* stop a note: low nibble is generator # */
#define CMD_INSTRUMENT  0xc0    /* change instrument; low nibble is generator #, instrument is next byte */
#define CMD_RESTART     0xe0    /* restart the score from the beginning */
#define CMD_STOP        0xf0    /* stop playing */
/* if CMD < 0x80, then the other 7 bits and the next byte are a 15-bit number of msec to delay */

/* these other commands stored in the track_status.com */
#define CMD_TEMPO       0xFE    /* tempo in usec per quarter note ("beat") */
#define CMD_TRACKDONE   0xFF    /* no more data left in this track */


/****************  utility routines  **********************/

/* portable string length */
int strlength (const char *str) {
   int i;
   for (i = 0; str[i] != '\0'; ++i);
   return i;
}

/* safe string copy */
size_t miditones_strlcpy (char *dst, const char *src, size_t siz) {
   char *d = dst;
   const char *s = src;
   size_t n = siz;
/* Copy as many bytes as will fit */
   if (n != 0) {
      while (--n != 0) {
         if ((*d++ = *s++) == '\0')
            break;
      }
   }
/* Not enough room in dst, add NUL and traverse rest of src */
   if (n == 0) {
      if (siz != 0)
         *d = '\0';             /* NUL-terminate dst */
      while (*s++);
   }
   return (s - src - 1);        /* count does not include NUL */
}

/* safe string concatenation */

size_t miditones_strlcat (char *dst, const char *src, size_t siz) {
   char *d = dst;
   const char *s = src;
   size_t n = siz;
   size_t dlen;
/* Find the end of dst and adjust bytes left but don't go past end */
   while (n-- != 0 && *d != '\0')
      d++;
   dlen = d - dst;
   n = siz - dlen;
   if (n == 0)
      return (dlen + strlength (s));
   while (*s != '\0') {
      if (n != 1) {
         *d++ = *s;
         n--;
      }
      s++;
   }
   *d = '\0';
   return (dlen + (s - src));   /* count does not include NUL */
}

/* match a constant character sequence */

int charcmp (const char *buf, const char *match) {
   int len, i;
   len = strlength (match);
   for (i = 0; i < len; ++i)
      if (buf[i] != match[i])
         return 0;
   return 1;
}

/* announce a fatal MIDI file format error */

void midi_error (char *msg, int curpos) {
   int ptr;
   fprintf (stderr, "---> MIDI file error at position %04X (%d): %s\n",
            (uint16_t) curpos, (uint16_t) curpos, msg);
/* print some bytes surrounding the error */
   ptr = curpos - 16;
   if (ptr < 0)
      ptr = 0;
   buffer.seek (buffer.data, ptr);
   for (int i = 0; i < 32; i++) {
      char c;
      buffer.read (buffer.data, &c, 1);
      fprintf (stderr, (ptr + i) == curpos ? " [%02X]  " : "%02X ", (int) c & 0xff);
   }
   fprintf (stderr, "\n");
   exit (8);
}

/* check that we have a specified number of bytes left in the buffer */

void chk_bufdata (int ptr, unsigned long int len) {
   if ((unsigned) (ptr + len) > buflen)
      midi_error ("data missing", ptr);
}

/* fetch big-endian numbers */

uint16_t rev_short (uint16_t val) {
   return ((val & 0xff) << 8) | ((val >> 8) & 0xff);
}

uint32_t rev_long (uint32_t val) {
   return (((rev_short ((uint16_t) val) & 0xffff) << 16) |
           (rev_short ((uint16_t) (val >> 16)) & 0xffff));
}

/**************  process the MIDI file header  *****************/

void process_header (void) {
   struct midi_header hdr;
   unsigned int time_division;

   chk_bufdata (hdrptr, sizeof (struct midi_header));
   buffer.seek (buffer.data, hdrptr);
   buffer.read (buffer.data, &hdr, sizeof (hdr));
   if (!charcmp ((char *) hdr.MThd, "MThd"))
      midi_error ("Missing 'MThd'", hdrptr);
   num_tracks = rev_short (hdr.number_of_tracks);
   time_division = rev_short (hdr.time_division);
   if (time_division < 0x8000)
      ticks_per_beat = time_division;
   else
      ticks_per_beat = ((time_division >> 8) & 0x7f) /* SMTE frames/sec */ *(time_division & 0xff);     /* ticks/SMTE frame */
   hdrptr += rev_long (hdr.header_size) + 8;    /* point past header to track header, presumably. */
   return;
}


/****************  Process a MIDI track header *******************/

void start_track (int tracknum) {
   struct track_header hdr;
   unsigned long tracklen;

   chk_bufdata (hdrptr, sizeof (struct track_header));
   buffer.seek (buffer.data, hdrptr);
   buffer.read (buffer.data, &hdr, sizeof (hdr));
   if (!charcmp ((char *) (hdr.MTrk), "MTrk"))
      midi_error ("Missing 'MTrk'", hdrptr);
   tracklen = rev_long (hdr.track_size);
   hdrptr += sizeof (struct track_header);      /* point past header */
   chk_bufdata (hdrptr, tracklen);
   track[tracknum].trkptr = hdrptr;
   hdrptr += tracklen;          /* point to the start of the next track */
   track[tracknum].trkend = hdrptr;     /* the point past the end of the track */
}

unsigned char buffer_byte (int offset) {
   unsigned char c;
   buffer.seek (buffer.data, offset);
   buffer.read (buffer.data, &c, 1);
   return c;
}

unsigned short buffer_short (int offset) {
   unsigned short s;
   buffer.seek (buffer.data, offset);
   buffer.read (buffer.data, &s, sizeof (short));
   return s;
}

unsigned int buffer_int32 (int offset) {
   uint32_t i;
   buffer.seek (buffer.data, offset);
   buffer.read (buffer.data, &i, sizeof (i));
   return i;
}

/* Get a MIDI-style variable-length integer */

unsigned long get_varlen (int *ptr) {
/* Get a 1-4 byte variable-length value and adjust the pointer past it.
These are a succession of 7-bit values with a MSB bit of zero marking the end */

   unsigned long val;
   int i, byte;

   val = 0;
   for (i = 0; i < 4; ++i) {
      byte = buffer_byte ((*ptr)++);
      val = (val << 7) | (byte & 0x7f);
      if (!(byte & 0x80))
         return val;
   }
   return val;
}


/***************  Process the MIDI track data  ***************************/

/* Skip in the track for the next "note on", "note off" or "set tempo" command,
then record that information in the track status block and return. */

void find_note (int tracknum) {
   unsigned long int delta_time;
   int event, chan;
   int note, velocity, controller, pressure, pitchbend, instrument;
   int meta_cmd, meta_length;
   unsigned long int sysex_length;
   struct track_status *t;
   char *tag;

/* process events */

   t = &track[tracknum];        /* our track status structure */
   while (t->trkptr < t->trkend) {

      delta_time = get_varlen (&t->trkptr);
      t->time += delta_time;
      if (buffer_byte (t->trkptr) < 0x80)
         event = t->last_event; /* using "running status": same event as before */
      else {                    /* otherwise get new "status" (event type) */
         event = buffer_byte (t->trkptr++);
      }
      if (event == 0xff) {      /* meta-event */
         meta_cmd = buffer_byte (t->trkptr++);
         meta_length = buffer_byte (t->trkptr++);
         switch (meta_cmd) {
         case 0x00:
            break;
         case 0x01:
            tag = "description";
            goto show_text;
         case 0x02:
            tag = "copyright";
            goto show_text;
         case 0x03:
            tag = "track name";
            goto show_text;
         case 0x04:
            tag = "instrument name";
            goto show_text;
         case 0x05:
            tag = "lyric";
            goto show_text;
         case 0x06:
            tag = "marked point";
            goto show_text;
         case 0x07:
            tag = "cue point";
          show_text:
            break;
         case 0x20:
            break;
         case 0x2f:
            break;
         case 0x51:            /* tempo: 3 byte big-endian integer! */
            t->cmd = CMD_TEMPO;
            t->tempo = rev_long (buffer_int32 (t->trkptr - 1)) & 0xffffffL;
            t->trkptr += meta_length;
            return;
         case 0x54:
            break;
         case 0x58:
            break;
         case 0x59:
            break;
         case 0x7f:
            tag = "sequencer data";
            goto show_hex;
         default:              /* unknown meta command */
            tag = "???";
          show_hex:
            break;
         }
         t->trkptr += meta_length;
      }

      else if (event < 0x80)
         midi_error ("Unknown MIDI event type", t->trkptr);

      else {
         if (event < 0xf0)
            t->last_event = event;      // remember "running status" if not meta or sysex event
         chan = event & 0xf;
         t->chan = chan;
         switch (event >> 4) {
         case 0x8:
            t->note = buffer_byte (t->trkptr++);
            velocity = buffer_byte (t->trkptr++);
          note_off:
            t->cmd = CMD_STOPNOTE;
            return;             /* stop processing and return */
         case 0x9:
            t->note = buffer_byte (t->trkptr++);
            velocity = buffer_byte (t->trkptr++);
            if (velocity == 0)  /* some scores use note-on with zero velocity for off! */
               goto note_off;
            t->velocity = velocity;
            t->cmd = CMD_PLAYNOTE;
            return;             /* stop processing and return */
         case 0xa:
            note = buffer_byte (t->trkptr++);
            velocity = buffer_byte (t->trkptr++);
            break;
         case 0xb:
            controller = buffer_byte (t->trkptr++);
            velocity = buffer_byte (t->trkptr++);
            break;
         case 0xc:
            instrument = buffer_byte (t->trkptr++);
            midi_chan_instrument[chan] = instrument;    // record new instrument for this channel
            break;
         case 0xd:
            pressure = buffer_byte (t->trkptr++);
            break;
         case 0xe:
            pitchbend = buffer_byte (t->trkptr) | (buffer_byte (t->trkptr + 1) << 7);
            t->trkptr += 2;
            break;
         case 0xf:
            sysex_length = get_varlen (&t->trkptr);
            t->trkptr += sysex_length;
            break;
         default:
            midi_error ("Unknown MIDI command", t->trkptr);
         }
      }
   }
   t->cmd = CMD_TRACKDONE;      /* no more notes to process */
   ++tracks_done;
}


/*  generate "stop note" commands for any channels that have them pending */

void gen_stopnotes (void) {
   struct tonegen_status *tg;
   for (int tgnum = 0; tgnum < num_tonegens; ++tgnum) {
      tg = &tonegen[tgnum];
      if (tg->stopnote_pending) {
         tsf_note_off (g_tsf, tg->instrument, tg->note);
         tg->stopnote_pending = false;
      }
   }
}

// State needed for PlayMID()
static int notes_skipped = 0;
static int tracknum = 0;
static int earliest_tracknum = 0;
static unsigned long earliest_time = 0;

// Open file, parse headers, get ready tio process MIDI
void PrepareMIDI(const char *soundfont, const char *midi)
{
   g_tsf = tsf_load_filename (soundfont);
   tsf_set_output (g_tsf, TSF_STEREO_INTERLEAVED, FREQ, -10 /* dB gain -10 */ );

   stdio.data = fopen(midi, "rb");
   stdio.read = (int (*)(void *, void *, unsigned int)) &tsf_stream_stdio_read;
   stdio.tell = (int (*)(void *)) &tsf_stream_stdio_tell;
   stdio.skip = (int (*)(void *, unsigned int)) &tsf_stream_stdio_skip;
   stdio.seek = (int (*)(void *, unsigned int)) &tsf_stream_stdio_seek;
   stdio.close = (int (*)(void *)) &tsf_stream_stdio_close;
   stdio.size = (int (*)(void *)) &tsf_stream_stdio_size;

   tsf_stream_wrap_cached(&stdio, 32, 64, &buffer);
   buflen = buffer.size (buffer.data);

/* process the MIDI file header */

   hdrptr = buffer.tell (buffer.data);  /* pointer to file and track headers */
   process_header ();
   printf ("  Processing %d tracks.\n", num_tracks);
   if (num_tracks > MAX_TRACKS)
      midi_error ("Too many tracks", buffer.tell (buffer.data));

/* initialize processing of all the tracks */

   for (tracknum = 0; tracknum < num_tracks; ++tracknum) {
      start_track (tracknum);   /* process the track header */
      find_note (tracknum);     /* position to the first note on/off */
   }

   notes_skipped = 0;
   tracknum = 0;
   earliest_tracknum = 0;
   earliest_time = 0;
}

// Parses the note on/offs ujntil we are ready to render some more samples.  Then return the
// total number of samples to render before we need to be called again
int PlayMIDI()
{
/* Continue processing all tracks, in an order based on the simulated time.
This is not unlike multiway merging used for tape sorting algoritms in the 50's! */

   do {                         /* while there are still track notes to process */
      static struct track_status *trk;
      static struct tonegen_status *tg;
      static int tgnum;
      static int count_tracks;
      static unsigned long delta_time, delta_msec;

      /* Find the track with the earliest event time,
         and output a delay command if time has advanced.

         A potential improvement: If there are multiple tracks with the same time,
         first do the ones with STOPNOTE as the next command, if any.  That would
         help avoid running out of tone generators.  In practice, though, most MIDI
         files do all the STOPNOTEs first anyway, so it won't have much effect.
       */

      earliest_time = 0x7fffffff;

      /* Usually we start with the track after the one we did last time (tracknum),
         so that if we run out of tone generators, we have been fair to all the tracks.
         The alternate "strategy1" says we always start with track 0, which means
         that we favor early tracks over later ones when there aren't enough tone generators.
       */

      count_tracks = num_tracks;
      do {
         if (++tracknum >= num_tracks)
            tracknum = 0;
         trk = &track[tracknum];
         if (trk->cmd != CMD_TRACKDONE && trk->time < earliest_time) {
            earliest_time = trk->time;
            earliest_tracknum = tracknum;
         }
      } while (--count_tracks);

      tracknum = earliest_tracknum;     /* the track we picked */
      trk = &track[tracknum];
      if (earliest_time < timenow)
         midi_error ("INTERNAL: time went backwards", trk->trkptr);

      /* If time has advanced, output a "delay" command */

      delta_time = earliest_time - timenow;
      if (delta_time) {
         gen_stopnotes ();      /* first check if any tone generators have "stop note" commands pending */
         /* Convert ticks to milliseconds based on the current tempo */
         unsigned long long temp;
         temp = ((unsigned long long) delta_time * tempo) / ticks_per_beat;
         delta_msec = temp / 1000;      // get around LCC compiler bug
         if (delta_msec > 0x7fff)
            midi_error ("INTERNAL: time delta too big", trk->trkptr);
         int samples = (((int) delta_msec) * FREQ) / 1000;
         timenow = earliest_time;
         return samples;
      }
      timenow = earliest_time;

      /*  If this track event is "set tempo", just change the global tempo.
         That affects how we generate "delay" commands. */

      if (trk->cmd == CMD_TEMPO) {
         tempo = trk->tempo;
         find_note (tracknum);
      }

      /*  If this track event is "stop note", process it and all subsequent "stop notes" for this track
         that are happening at the same time. Doing so frees up as many tone generators as possible.  */

      else if (trk->cmd == CMD_STOPNOTE)
         do {
            // stop a note
            for (tgnum = 0; tgnum < num_tonegens; ++tgnum) {    /* find which generator is playing it */
               tg = &tonegen[tgnum];
               if (tg->playing && tg->track == tracknum && tg->note == trk->note) {
                  tg->stopnote_pending = true;  /* must stop the current note if another doesn't start first */
                  tg->playing = false;
                  trk->tonegens[tgnum] = false;
               }
            }
            find_note (tracknum);       // use up the note
         } while (trk->cmd == CMD_STOPNOTE && trk->time == timenow);

      /*  If this track event is "start note", process only it.
         Don't do more than one, so we allow other tracks their chance at grabbing tone generators. */

      else if (trk->cmd == CMD_PLAYNOTE) {
         bool foundgen = false;
         /* if not, then try for any free tone generator */
         if (!foundgen)
            for (tgnum = 0; tgnum < num_tonegens; ++tgnum) {
               tg = &tonegen[tgnum];
               if (!tg->playing) {
                  foundgen = true;
                  break;
               }
            }
         if (foundgen) {
            if (tgnum + 1 > num_tonegens_used)
               num_tonegens_used = tgnum + 1;
            tg->playing = true;
            tg->track = tracknum;
            tg->note = trk->note;
            tg->stopnote_pending = false;
            trk->tonegens[tgnum] = true;
            trk->preferred_tonegen = tgnum;
            if (tg->instrument != midi_chan_instrument[trk->chan]) {    /* new instrument for this generator */
               tg->instrument = midi_chan_instrument[trk->chan];
            }
            tsf_note_on (g_tsf, tg->instrument, tg->note, trk->velocity / 256.0);
         } else {
            ++notes_skipped;
         }
         find_note (tracknum);     // use up the note
      }
   }
   while (tracks_done < num_tracks);
   gen_stopnotes ();            /* flush out any pending "stop note" commands */
   return -1; // EOF
}


void StopMIDI()
{

   buffer.close(buffer.data);
   tsf_close(g_tsf);
   printf ("  %s %d tone generators were used.\n",
           num_tonegens_used < num_tonegens ? "Only" : "All", num_tonegens_used);
   if (notes_skipped)
      printf
         ("  %d notes were skipped because there weren't enough tone generators.\n", notes_skipped);

   printf ("  Done.\n");
}


bool doneplaying = false;

// Callback function called by the audio thread
static void audioCB(void* data, Uint8 *stream, int len)
{
   static int samplesLeft = 0;
   static bool eof = false;
   int cnt = (len / (2 * sizeof(short))); //2 output channels
   short *ptr = (short *)stream;
   while (cnt) {
      if (samplesLeft) {
         int togen = (samplesLeft >= cnt) ? cnt : samplesLeft;
         tsf_render_short(g_tsf, ptr, togen, 0);
         ptr += togen;
         cnt -= togen;
         samplesLeft -= togen;
         if (!cnt) return;
      }
      if (!eof) samplesLeft = PlayMIDI();
      if (samplesLeft == -1) {
         eof = true;
         samplesLeft = FREQ/2; // 0.5 second fade
      } else if (samplesLeft==0 && eof) {
         doneplaying = true;
         memset(ptr, 0, sizeof(short) * 2 * cnt);
         return;
      }
   }
}


void usage()
{
   printf("Usage: midiplay --sf <sounffont.sf2> --midi <song.mid> [--profile]\n");
   exit(1);
}

int main(int argc, char **argv)
{
   char *soundfont = NULL;
   char *midi = NULL;
   bool profile = false;

   for (int i=1; i<argc; i++) {
      if (!strcmp(argv[i], "--sf")) {
         soundfont = argv[i+1];
         i++;
      } else if (!strcmp(argv[i], "--midi")) {
         midi = argv[i+1];
         i++;
      } else if (!strcmp(argv[i], "--profile")) {
         profile = true;
      } else {
         printf("Unknown parameter: %s\n", argv[i]);
         usage();
      }
   }
   if (!soundfont || !midi) {
      printf("ERROR: Please specify soundfont and midi file.\n");
      usage();
   }

   if (profile) {
      short *data = (short*)malloc(1024*1024);
      PrepareMIDI(soundfont, midi);
      do {
         int samples = PlayMIDI();
         if (samples==-1) break;
         tsf_render_short(g_tsf, data, samples, 0);
      } while (1);
      tsf_render_short(g_tsf, data, 44100/2, 0);
      free(data);
      return 0;
   }


   // Define the desired audio output format we request
   SDL_AudioSpec OutputAudioSpec;
   OutputAudioSpec.freq = FREQ;
   OutputAudioSpec.format = AUDIO_S16;
   OutputAudioSpec.channels = 2;
   OutputAudioSpec.samples = 4096;
   OutputAudioSpec.callback = audioCB;

   // Initialize the audio system
   if (SDL_AudioInit(NULL) < 0)
   {
      fprintf(stderr, "Could not initialize audio hardware or driver\n");
      return 1;
   }

   PrepareMIDI( soundfont, midi );

   // Request the desired audio output format
   if (SDL_OpenAudio(&OutputAudioSpec, NULL) < 0)
   {
      fprintf(stderr, "Could not open the audio hardware or the desired audio output format\n");
      return 1;
   }

   // Start the actual audio playback here
   // The audio thread will begin to call our AudioCallback function
   SDL_PauseAudio(0);

   while (!doneplaying) {
      SDL_Delay(1000);
   }

   StopMIDI();

   return 0;
}
