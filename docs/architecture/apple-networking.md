# Apple HTTP networking

The Apple Penumbra transport uses an ephemeral `URLSession` configured without a
cookie store or response cache. It delivers each data delegate callback as a
timestamped HTTP chunk. `HttpRequestHandle::Cancel` suppresses future callbacks
immediately, and task suspension supplies the transport pause and resume
operations. URL ranges become `Range` request headers.

The transport reads its platform policy from these shared settings:

| Setting | Default | URLSession behaviour |
|---------|---------|----------------------|
| `network.http.allows_constrained_network_access` | `true` | Permits Low Data Mode routes when enabled. |
| `network.http.allows_expensive_network_access` | `true` | Permits expensive routes, including cellular, when enabled. |
| `network.http.assume_http3_capable` | `false` | Leaves HTTP/3 capability negotiation to URLSession unless enabled. |

The settings use the shared `network.http.*` namespace because ADR-0017
prohibits platform names in setting keys. Their schema availability is limited
to Apple platforms; other platforms ignore them.

URLSession reports task transaction metrics for DNS, connection, TLS and the
negotiated protocol. Chunk arrival times use the monotonic clock at the delegate
handoff. URLSession exposes no separate connect-timeout control, so the
per-request resource timeout uses `network.http.connect_timeout_ms` and the
configuration idle timeout uses `network.http.read_timeout_ms`.

## App Transport Security

`NSAllowsArbitraryLoadsForMedia` applies to media loaded by AVFoundation. It
does not grant clear-text HTTP access to the Alumbra URLSession transport.
Applications that fetch `http://` media through Alumbra must add an explicit
App Transport Security exception for the relevant host, or use HTTPS. Keep
exceptions scoped to the hosts and domains that require them; do not enable an
unrestricted arbitrary-loads exception for the player.

For example, an application that must load clear-text media from
`media.example.com` can add this domain exception to its `Info.plist`:

```xml
<key>NSAppTransportSecurity</key>
<dict>
  <key>NSExceptionDomains</key>
  <dict>
    <key>media.example.com</key>
    <dict>
      <key>NSExceptionAllowsInsecureHTTPLoads</key>
      <true/>
    </dict>
  </dict>
</dict>
```
