\file penumbra/interface/shared_bytes.h
Immutable, reference-counted byte ranges that move between sequences without copying.

\class penumbra::SharedBytes
Immutable, reference-counted range of bytes.

Copies share the same bytes. The reference count is atomic, so a range can move between sequences, and the
bytes are never written after creation. A default-constructed range is empty.

\fn penumbra::SharedBytes::SharedBytes()
Creates an empty range with a null data().

\fn penumbra::SharedBytes::SharedBytes(const SharedBytes&)
Shares the bytes of another range without copying them.

\fn SharedBytes& penumbra::SharedBytes::operator=(const SharedBytes&)
Shares the bytes of another range and releases this range's reference to its previous bytes.

\fn penumbra::SharedBytes::SharedBytes(SharedBytes&& other) noexcept
Takes over the range of `other`, leaving `other` empty with a null data() and a size() of 0.

\fn SharedBytes& penumbra::SharedBytes::operator=(SharedBytes&& other) noexcept
Replaces this range with the range of `other`, leaving `other` empty.

\fn static SharedBytes penumbra::SharedBytes::Adopt(std::vector<uint8_t> bytes)
Takes ownership of the bytes of a vector without copying them.

\fn static SharedBytes penumbra::SharedBytes::Copy(std::span<const uint8_t> bytes)
Copies bytes into a new range.

\fn static SharedBytes penumbra::SharedBytes::Wrap(std::shared_ptr<const void> owner, std::span<const uint8_t> bytes)
Shares memory that `owner` keeps alive, such as a network buffer or a platform object.

The bytes must stay valid and unchanged for as long as `owner` lives.

\fn std::optional<SharedBytes> penumbra::SharedBytes::Slice(size_t offset, size_t size) const
Returns `size` bytes starting at `offset`, sharing the same storage.

Returns `std::nullopt` when the requested range does not lie within this one.

\fn const uint8_t* penumbra::SharedBytes::data() const
Returns a pointer to the first byte, or null for a default-constructed or moved-from range.

\fn size_t penumbra::SharedBytes::size() const
Returns the number of bytes.

\fn bool penumbra::SharedBytes::empty() const
Returns whether the range has no bytes.

\fn std::span<const uint8_t> penumbra::SharedBytes::span() const
Returns the bytes as a span, valid while this range or a copy of it is alive.
