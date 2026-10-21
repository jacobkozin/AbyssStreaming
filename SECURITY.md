# Security policy

## Supported versions

Security fixes go to the latest code on the default branch. There are no maintained release branches.

## Reporting a vulnerability

Use GitHub's private vulnerability reporting if it is enabled for this repository. Otherwise, open a minimal issue asking for a private contact channel, without exploit details, credentials or logs.

Never post real Twitch, YouTube, Kick or other RTMP stream keys anywhere. If a key is exposed, reset it in the platform's dashboard right away.

## How stream keys are handled

- Keys are typed into a masked field and stored in **Windows Credential Manager** as generic credentials named `AbyssStream/stream-key/<destination-id>`. Windows encrypts them with the signed-in user's credentials. You can view or delete them in Control Panel > Credential Manager > Windows Credentials.
- `settings.json` never contains a key; a unit test enforces that. Removing a destination in the app also deletes its stored key.
- Every line written to `AbyssStream-obs.log` passes through a redactor that masks all known keys, including a key pasted into a server URL or echoed back by a server. Error messages shown in the app are masked the same way.
- Abyss Stream sends nothing anywhere except to the RTMP servers you configure. No telemetry, no account, no relay.

Crash dumps (`AbyssStream-crash-*.dmp`) contain process memory, which can include a key that was in use. Treat them as private and share them only with someone you trust.
