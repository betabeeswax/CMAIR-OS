# CMAIR-OS App SDK (draft)

The SDK contract is being designed for portable apps. App metadata is JSON; native app binaries are target-specific unless written against a portable runtime.

## Draft manifest

```json
{
  "id": "example-app",
  "name": "Example App",
  "version": "0.1.0",
  "targets": ["cardputer-adv", "desktop"],
  "entry": "main",
  "capabilities": ["display", "input", "storage.appdata"]
}
```

## Rules

- Use stable app IDs; display names can contain spaces.
- Request only the capabilities actually needed.
- Store user data in the app-scoped data directory.
- Handle storage removal, low memory and app suspension.
- Avoid blocking the UI loop.
- Provide a desktop test path for portable logic.
- Do not assume network access.

This SDK is a draft. The ABI and package format will be versioned before third-party apps are treated as stable.
