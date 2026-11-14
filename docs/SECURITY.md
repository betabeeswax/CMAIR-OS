# Security model

- Apps declare capabilities in their manifest.
- Sensitive capabilities (microphone, network, IR, Bluetooth, remote input and broad file access) require user approval.
- Passwords and tokens must use platform secure storage where available; never plain-text files.
- App packages should be checksummed and, when signing infrastructure is added, signature-verified before installation.
- Updates need rollback/recovery behavior.
- Remote control must be opt-in, authenticated and visibly indicated.
- Logs must not include passwords, tokens or private message contents.
