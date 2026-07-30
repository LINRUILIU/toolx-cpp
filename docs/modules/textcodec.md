# textcodec

> Audience: C++ consumers encoding small text/binary values
> Status: Stable support module guide
> Applies to: `v0.3.2` release candidate
> Source of truth for: codec options and decode-error behavior

## Role and boundary

`textcodec` implements hex, Base64, Base64 URL-safe, and URL percent encoding
with explicit decode policy and errors. It is not a character-set converter,
Unicode normalizer, compression layer, or cryptographic facility.

- CMake target: `toolx::textcodec`
- Direct dependency: `toolx::utils`; transitive ToolX dependencies: none
- Stability: stable support
- Header: [`include/textcodec.h`](../../include/textcodec.h)

## Quick start

```cpp
textcodec::Base64Options options;
options.variant = textcodec::Base64Variant::UrlSafe;
options.padding = false;
const auto encoded = textcodec::base64_encode("payload", options);
const auto decoded = textcodec::base64_decode(encoded, options);
```

## Capability and API matrix

| Area | Public API | Policy controls |
| --- | --- | --- |
| Hex | `hex_encode`, `hex_encode_bytes`, `hex_decode`, `hex_decode_to_buffer` | Uppercase output and explicit buffer capacity |
| Base64 | `base64_encode`, `base64_decode` | `Base64Variant`, padding through `Base64Options` |
| URL | `url_encode`, `url_decode` | `%20` versus plus and plus-preservation/space decoding |
| Errors | `DecodeResult<T>`, `DecodeError`, `ToString` | Invalid character/padding/truncation/percent/policy/range categories |

## Behavior and safety

- Decode failures are data and do not throw through the public contract.
- Buffer decoding reports capacity errors without writing beyond the supplied
  capacity.
- Base64 padding and URL plus behavior must use matching options when a protocol
  requires a specific variant.
- Codecs provide representation changes only; they do not authenticate,
  encrypt, compress, or validate character encodings.
- Stateless functions are reentrant. Caller-owned buffers and result values
  have ordinary C++ lifetime rules.

## Stability, compatibility, examples, and version changes

The documented codecs and option policies are stable support. No
`textcodec`-specific user-visible change is recorded for `v0.3.2`.

- [Annotated cookbook](../../examples/textcodec_cookbook.cpp)
- [Basic example](../../examples/textcodec_example.cpp)
- [Behavior tests](../../tests/textcodec_tests.cpp)
