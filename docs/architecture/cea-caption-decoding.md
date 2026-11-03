# CEA caption decoding

`umbra::text::CeaDecoder` accepts ATSC `cc_data` triplets in presentation order. Its snapshots identify the
608 channel or 708 service and carry grid placement and the presentation timestamp. The demuxer remains
responsible for extracting `cc_data` from video SEI and ordering it by presentation time.

CEA-608 uses a 32-column by 15-row screen. The decoder handles pop-on, roll-up, paint-on, erase, carriage
return, PAC positioning, tab offsets, special characters and repeated controls. CEA-708 assembles bounded
DTVCC packets, dispatches service blocks, tracks the eight windows and decodes basic characters and window
anchors. Unsupported commands are skipped within their bounded service block.

The decoder returns snapshots rather than modifying a player track. The caller can expose each channel or
service as a separate caption track and translate each snapshot into the text cue model.
