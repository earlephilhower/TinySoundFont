echo Building \'example1-linux-`uname -m`\' ...
gcc -g -Wall example1.c minisdl_audio.c -lm -ldl -lpthread -o example1-linux-`uname -m`
echo Building \'example2-linux-`uname -m`\' ...
gcc -g -Wall example2.c minisdl_audio.c -lm -ldl -lpthread -o example2-linux-`uname -m`
echo Done!
