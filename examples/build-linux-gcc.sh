#echo Building \'example1-linux-`uname -m`\' ...
rm -f example1-linux-`uname -m`
gcc -g -Wall example1.c minisdl_audio.c -lm -ldl -lpthread -o example1-linux-`uname -m`
#echo Building \'example2-linux-`uname -m`\' ...
rm -f example2-linux-`uname -m`
gcc -g -Wall example2.c minisdl_audio.c -lm -ldl -lpthread -o example2-linux-`uname -m`
rm -f miditsf
gcc -g -Wall miditsf.c -lm -o miditsf
rm -f miditones
gcc -g -Wall miditones.c -lm -o miditones
rm -f midiplay
gcc -g -Wall midiplay.c -lm -o midiplay
#echo Done!
