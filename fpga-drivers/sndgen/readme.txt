```
============================================================

The sound generator peripheral attempts to recreate the sounds
found in 1980's video arcades.  It has an oscillator that can
be frequency modulated by a low frequency oscillator.  It has
a white noise generator that can be added to the output of the
oscillator.  Both the oscillator and the noise generator allow
attenuation of zero, one-half, one-fourth, or one-eighth.

 
HARDWARE
    The generator outputs four bits of audio data that can be
directed to a four bit R-2R ladder network for digital to
analog conversion.  In the simplest case just the MSB can be
used as an audio source.


RESOURCES
    A single 'config' resource controls all aspects of the sound
generator.  There are nine configuration parameters.  For example:
    pcset sndgne config s 100 d 10 40 c o 0 0

The first parameter is the desired shape of the output waveform.
One of 'otsrf' where 'o' turns the main oscillator off, 't' gives
a triangle waveform, 's' gives a square wave, 'r' is a rising ramp,
and 'f' is a falling ramp.

The second parameter is the frequency of the main oscillator in
the range of 24 to 7000 Hertz.

The third parameter is the waveform of the LFO (low frequency
oscillator).  The LFO frequency modulates the main oscillator.
The waveform is specified as one of 'otrfud' where 'o' turns the LFO
off, 't' gives it a triangle waveform, 'r' is a rising ramp, 'f' is
a falling ramp, 'u' is square wave with a step up in frequency, and
'd' is a square wave with a step down in frequency.

The fourth parameter is the total frequency to add or subtract from
the main oscillator.  The LFO frequency is added for a LFO step up
and rising ramp, and subtracted for a step down or a falling ramp.
The LFO frequency must be in the range of 0 to 5000 Hertz.

The fifth parameter is the duration of LFO ramp or square wave. This
is in hundredths of a second and can be between 0 and 250.  

The sixth parameter controls the LFO and output and must be one of
'c' or 'o'.  A 'c' enables continuous output.  An 'o' is one-shot
mode where the output stops at the end of the LFO duration.

The seventh parameter sets the noise frequency and must be one of
'ohml' where 'o' turns off the noise generator, 'h' give a high
frequency hiss, 'm' give a static sound, and 'l' give a crackling
sound.

The eighth and ninth parameters specify the attenuation to apply
to the oscillator and noise outputs.  This is a single character
and one of '0248' where '0' applies no attenuation, '2' divides
the output by two, '4' divides by four, and '8' divides by eight.
These parameters only make sense when feeding a DAC and all four
bits of the output.


EXAMPLES
A one kilohertz square wave, no LFO, no noise and continuous 
output:
    pcset sndgen config s 1000 o 0 0 c o 0 0

A 440 Hertz square wave, LFO rising ramp modulation with a
LFO frequency of 200 Hertz lasting 200 milliseconds:
    pcset sndgen config s 440 r 100 200 o o 0 0

A short burst of white noise
    pcset sndgen config o 100 o 0 20 o m 0 0

A continuous warbling sound going from 440 Hertz to 880 Hertz
and back to 440 Hertz every 0.20 seconds:
    pcset sndgen config s 440 t 440 20 c o 0 0

Going down by the oscillator frequency effectively turn off 
the oscillator.  A beep lasting 0.5 seconds followed by 0.5
seconds of silence:
    pcset sndgen config s 1000 d 1000 100 c o 0 0



```
