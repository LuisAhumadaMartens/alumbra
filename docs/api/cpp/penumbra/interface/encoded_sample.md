\file penumbra/interface/encoded_sample.h
The compressed access unit that Umbra writes to a port.

A demuxer slices samples out of a downloaded segment. Copying a sample copies references to its bytes, so the
bytes reach the port without being copied.

\include share_sample_bytes.cc

\struct penumbra::EncodedSample
One compressed access unit with its container timing and metadata.

Timestamps stay in the time base of the stream.

\var uint32_t penumbra::EncodedSample::track_id
Track of the sample, matching StreamInfo::track_id.

\var SharedBytes penumbra::EncodedSample::data
Compressed bytes.

\var Timestamp penumbra::EncodedSample::pts
Presentation timestamp; unknown by default.

\var Timestamp penumbra::EncodedSample::dts
Decode timestamp; unknown by default.

\var Timestamp penumbra::EncodedSample::duration
Duration; unknown by default.

\var bool penumbra::EncodedSample::key_frame
Whether decoding can start at this sample.

\var bool penumbra::EncodedSample::discontinuity
Whether the sample follows a discontinuity in the timeline.

\var std::shared_ptr<const StreamInfo> penumbra::EncodedSample::config_change
New stream description when the configuration changes in-band at this sample, or null.

The track ID stays the same. A port that cannot apply the change in place rejects the write with
`kUnimplemented`, and the player is recreated instead.

\var Timestamp penumbra::EncodedSample::trim_start
Encoder priming to remove from the start of the decoded output; unknown means none.

\var Timestamp penumbra::EncodedSample::trim_end
Padding to remove from the end of the decoded output; unknown means none.

\var SharedBytes penumbra::EncodedSample::hdr_dynamic_metadata
HDR dynamic metadata of the sample, or empty.

\var std::optional<EncryptionInfo> penumbra::EncodedSample::encryption
Encryption metadata, or empty for a clear sample.

\fn std::string penumbra::EncodedSample::ToString() const
Formats the track, timestamps, size and flags for diagnostics, without the sample bytes.

\fn template <typename Sink> void penumbra::EncodedSample::AbslStringify(Sink& sink, const EncodedSample& sample)
Lets Abseil formatting and logging print the sample as ToString() does.

\fn EncryptionCheck penumbra::ValidateEncryption(const EncodedSample& sample)
Checks the encryption metadata of `sample` against its size; a clear sample is valid.
