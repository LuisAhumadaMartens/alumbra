# Audio conversion

The desktop port converts decoded audio in penumbra/desktop/audio_converter.*.
It uses FFmpeg's shared libswresample library; Umbra and installed public
headers do not depend on FFmpeg. Other ports may implement the same conversion
contract with native converters.

The converter accepts signed 16-bit, signed 32-bit and float samples in planar
or interleaved form and produces interleaved float output. It retains channel
order from native masks or explicit channel-position descriptors, including
the Opus mapping-family-1 5.1 order. A missing layout uses FFmpeg's default
layout for the channel count. Downmix to stereo uses the
ITU-R BS.775 coefficients: centre and surround channels contribute at
approximately −3 dB, and the LFE channel is omitted. The none policy requires
the output channel count to match the input. The stereo policy always selects
two output channels.

Resampler presets select filter sizes of 16, 32 and 64 taps for low, medium and
high quality. Output timestamps begin at the input presentation time and
advance by the number of converted sample frames at the output rate. A caller
must keep a single converter for a continuous stream, drain its delayed samples
at end of stream and reset it on seek or flush.

Volume and mute changes apply a linear sample-frame gain ramp. The time-stretch
entry point sets a resampling compensation ratio from 0.25 to 4.0; this changes
pitch and is a hook for the pitch-preserving implementation in #92.

The default stereo downmix deliberately excludes LFE. When FFmpeg supplies
downmix metadata with a decoded frame, its centre, surround and LFE coefficients
override those defaults.
