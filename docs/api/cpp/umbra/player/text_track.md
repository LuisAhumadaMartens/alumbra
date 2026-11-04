\file umbra/player/text_track.h
Public cue and timed-text-track types.

`TextTrack` stores cues in start-time order and uses half-open active intervals. `Player` refreshes active cues after
seeks, when `current_time()` is read and before `DispatchEvents()` delivers queued work. Track-list listeners receive
addtrack or removetrack followed by change.

\enum umbra::player::TextTrackKind
The HTML text-track category. Values are subtitles, captions, descriptions, chapters and metadata.

\var umbra::player::TextTrackKind::kSubtitles
Subtitles containing dialogue or translation.

\var umbra::player::TextTrackKind::kCaptions
Captions containing dialogue and relevant sound information.

\var umbra::player::TextTrackKind::kDescriptions
Descriptions of visual information.

\var umbra::player::TextTrackKind::kChapters
Chapter navigation cues.

\var umbra::player::TextTrackKind::kMetadata
Application-defined timed metadata.

\enum umbra::player::TextTrackMode
Controls whether a track is disabled, updates cues without presentation, or is selected for presentation.

\var umbra::player::TextTrackMode::kDisabled
Track is excluded from active-cue computation.

\var umbra::player::TextTrackMode::kHidden
Track computes active cues without presentation.

\var umbra::player::TextTrackMode::kShowing
Track is selected for presentation.

\enum umbra::player::TextTrackListEventType
Identifies an addtrack, removetrack or change notification.

\var umbra::player::TextTrackListEventType::kAddTrack
A track was added.

\var umbra::player::TextTrackListEventType::kRemoveTrack
A track was removed.

\var umbra::player::TextTrackListEventType::kChange
The track list changed.

\struct umbra::player::StyledTextRun
A cue text segment with voice, language and author emphasis.

\var umbra::player::StyledTextRun::text
Text content of the run.

\var umbra::player::StyledTextRun::voice
Voice annotation, when available.

\var umbra::player::StyledTextRun::language
Language tag for the run.

\var umbra::player::StyledTextRun::classes
WebVTT class names applied to the run.

\var umbra::player::StyledTextRun::timestamp
The cue-relative text timestamp in seconds when present.

\var umbra::player::StyledTextRun::has_timestamp
Whether the run follows a WebVTT text timestamp tag.

\var umbra::player::StyledTextRun::bold
Whether the run is bold.

\var umbra::player::StyledTextRun::italic
Whether the run is italic.

\var umbra::player::StyledTextRun::underline
Whether the run is underlined.

\var umbra::player::StyledTextRun::font_family
Author font family requested by the cue source.

\var umbra::player::StyledTextRun::font_size
Author font size in the cue source's layout units.

\var umbra::player::StyledTextRun::foreground_argb
Run foreground colour in ARGB order.

\var umbra::player::StyledTextRun::has_foreground_color
Whether the source supplied a foreground colour.

\var umbra::player::StyledTextRun::color
Author colour from supported SubRip font tags, when present.

\struct umbra::player::TextCue
A positioned text cue with timed interval, styled runs, direction and ruby annotation.

\var umbra::player::TextCue::id
Cue identity within its track.

\var umbra::player::TextCue::start_time
Inclusive start time in seconds.

\var umbra::player::TextCue::end_time
Exclusive end time in seconds.

\var umbra::player::TextCue::runs
Styled text runs in reading order.

\var umbra::player::TextCue::region
Region identifier, when supplied by the source format.

\var umbra::player::TextCue::line
Line position.

\var umbra::player::TextCue::position
Inline position as a percentage.

\var umbra::player::TextCue::size
Cue box size as a percentage.

\var umbra::player::TextCue::alignment
Block alignment from WebVTT cue settings.

\var umbra::player::TextCue::position_alignment
Inline position alignment from WebVTT cue settings.

\var umbra::player::TextCue::line_alignment
Line alignment from WebVTT cue settings.

\var umbra::player::TextCue::snap_to_lines
Whether the cue line is measured in line units rather than a percentage.

\var umbra::player::TextCue::fade_in_seconds
Fade-in duration in seconds when supplied by the source format.

\var umbra::player::TextCue::fade_out_seconds
Fade-out duration in seconds when supplied by the source format.

\var umbra::player::TextCue::vertical
Whether the cue uses vertical writing.

\var umbra::player::TextCue::right_to_left
Whether text direction is right to left.

\var umbra::player::TextCue::ruby_annotation
Ruby annotation associated with the cue.

\struct umbra::player::BitmapCue
An RGBA bitmap cue with timed interval and dimensions.

\var umbra::player::BitmapCue::id
Cue identity within its track.

\var umbra::player::BitmapCue::start_time
Inclusive start time in seconds.

\var umbra::player::BitmapCue::end_time
Exclusive end time in seconds.

\var umbra::player::BitmapCue::rgba
RGBA pixel payload.

\var umbra::player::BitmapCue::width
Bitmap width in pixels.

\var umbra::player::BitmapCue::height
Bitmap height in pixels.

\class umbra::player::TextTrack
Owns text and bitmap cues, computes active cues and emits cue-change notifications.

\fn umbra::player::TextTrack::TextTrack(TextTrackKind kind, std::string label, std::string language, size_t memory_budget_bytes)
Creates a track with the supplied metadata and per-track retained-payload budget.

\fn TextTrackKind umbra::player::TextTrack::kind() const
Returns the track category.

\fn const std::string& umbra::player::TextTrack::label() const
Returns the display label.

\fn const std::string& umbra::player::TextTrack::language() const
Returns the BCP 47 language tag.

\fn const std::string& umbra::player::TextTrack::id() const
Returns the unique track identifier.

\fn TextTrackMode umbra::player::TextTrack::mode() const
Returns the current mode.

\fn void umbra::player::TextTrack::set_mode(TextTrackMode mode)
Changes the mode and notifies when the active-cue set changes.

\fn bool umbra::player::TextTrack::LoadSubRip(std::string bytes, std::string fallback_encoding, bool auto_detect)
Parses and stores a SubRip source, replacing the track's current cues. Returns false when decoding fails or the cue memory budget is exceeded.

\fn bool umbra::player::TextTrack::SetEncodingOverride(std::string encoding)
Re-decodes the retained SubRip source with the supplied character encoding without loading the source again. Returns false when the track has no SubRip source or the encoding is unsupported.

\fn const std::string& umbra::player::TextTrack::TextEncoding() const
Returns the encoding selected for the current SubRip cues, or an empty string before a SubRip source is loaded.

\fn double umbra::player::TextTrack::TextEncodingConfidence() const
Returns the parser confidence for the selected encoding, between zero and one.

\fn bool umbra::player::TextTrack::AddCue(TextCue cue)
Adds and orders a cue, evicting expired cues if needed. Returns false for invalid times or when the budget cannot hold it.

\fn bool umbra::player::TextTrack::AddBitmapCue(BitmapCue cue)
Adds and orders a bitmap cue, evicting expired cues if needed. Returns false for invalid times, dimensions or budget.

\fn void umbra::player::TextTrack::RemoveCue(uint64_t id)
Removes a text or bitmap cue with the identifier.

\fn void umbra::player::TextTrack::Update(double media_time)
Recomputes active cues at the media time and notifies once when their identities change.

\fn std::vector<TextCue> umbra::player::TextTrack::cues() const
Returns text cues in start-time order.

\fn std::vector<TextCue> umbra::player::TextTrack::active_cues() const
Returns active text cues in start-time order.

\fn std::vector<BitmapCue> umbra::player::TextTrack::bitmap_cues() const
Returns bitmap cues in start-time order.

\fn size_t umbra::player::TextTrack::memory_usage_bytes() const
Returns the estimated retained cue storage.

\fn Subscription umbra::player::TextTrack::AddCueChangeListener(std::function<void()> listener)
Registers a listener invoked when the active-cue identities change.

\class umbra::player::TextTrackList
Ordered collection of shared text tracks.

\fn size_t umbra::player::TextTrackList::length() const
Returns the number of tracks.

\fn std::shared_ptr<TextTrack> umbra::player::TextTrackList::item(size_t index) const
Returns the indexed track or null when out of range.

\fn void umbra::player::TextTrackList::Add(std::shared_ptr<TextTrack> track)
Adds a non-null track and notifies list listeners.

\fn bool umbra::player::TextTrackList::Remove(const std::string& id)
Removes the matching track and returns whether one was found.

\fn Subscription umbra::player::TextTrackList::AddChangeListener(std::function<void()> listener)
Registers a listener for list changes.

\fn Subscription umbra::player::TextTrackList::AddEventListener(std::function<void(TextTrackListEventType)> listener)
Registers a listener for addtrack, removetrack and change events, in that order for each operation.
