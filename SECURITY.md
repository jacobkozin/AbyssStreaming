# Security policy

## Supported versions

Abyss Stream is pre-release software. Security fixes are applied to the latest code on the default branch; no stable release line is currently supported.

## Reporting a vulnerability

Use GitHub's private vulnerability-reporting feature if it is available for this repository. If it is not available, open a minimal issue requesting a private contact channel without including exploit details, credentials, logs, or other sensitive material.

Never submit real Twitch, YouTube, Kick, or custom RTMP stream keys. Revoke and rotate a key immediately if it is exposed anywhere public.

## Credential handling

The current prototype keeps stream keys in process memory and does not intentionally persist them. Persistent credential storage is not implemented yet and must use an operating-system-backed secret store such as Windows Credential Manager or DPAPI—not a plaintext configuration file.
